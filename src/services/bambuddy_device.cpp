#include "bambuddy_device.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/idf_additions.h>
#include <math.h>
#include <string.h>

#include "app/app_state.h"
#include "app_config.h"
#include "hardware/scale.h"
#include "hardware/scale_state.h"
#include "hardware/sd_logger.h"
#include "services/auto_weight_state.h"
#include "services/bambuddy_api.h"
#include "services/bambuddy_device_rules.h"
#include "services/backend.h"
#include "services/user_options.h"
#include "services/wifi_manager.h"
#include "services/worker_task.h"

// The heartbeat's rate, the weight's and the timeouts of both are in
// services/bambuddy_device_rules.h, with the rules that space them out.
// A failed registration is retried, but not on every pass - a server that is
// up and refusing the key would otherwise produce a request per loop.
#define BB_REGISTER_RETRY_MS  30000
// Live weight for the web interface: only when the reading moved far enough
// to be worth a request.
#define BB_WEIGHT_MIN_DELTA_G     2.0f
// The report task (Spec 5.3 AC a03, V71). Its stack lies in PSRAM: a task
// that lives as long as the device with 12 kB of internal RAM would take the
// 7B from 45 kB free in operation to 33 kB, below what the update check
// needs to start (beta.4 design note, E1). It never touches the flash itself
// - its log lines go to the loop through logSD()'s queue - so a stack in
// PSRAM is safe (worker_task.h). Priority as the loop's, on the other core.
#define BB_TASK_STACK_BYTES   12288
#define BB_TASK_PRIORITY          1
#define BB_TASK_CORE              0
// A task that could not be had is tried again at most once a minute.
#define BB_TASK_RETRY_MS      60000

static bool          s_registered      = false;
static unsigned long s_last_register_ms = 0;
static unsigned long s_last_hb_ms       = 0;
static bool          s_last_hb_ok       = false;
static unsigned long s_last_weight_ms   = 0;
static uint32_t      s_weight_gap_ms    = BB_WEIGHT_MIN_GAP_MS;
static uint32_t      s_hb_gap_ms        = BB_HEARTBEAT_MS;
static float         s_last_weight_g    = -9999.0f;
// Spread of the readings since the last report, which is what decides
// whether the value handed to the web interface counts as settled.
static float         s_win_min          =  1e9f;
static float         s_win_max          = -1e9f;
static char          s_reported_tag[40] = "";

// ---- the report task -----------------------------------------------------
// One request at a time. The loop fills the slot and wakes the task, the task
// runs the call and marks the slot done, the loop picks the answer up on a
// later pass. Neither side touches a slot the other one owns. Commands that
// come back with a heartbeat (tare, restart) are carried out by the loop.
enum BbReportKind : uint8_t { BBR_HEARTBEAT, BBR_WEIGHT };
enum BbReportSlot : uint8_t { BBR_IDLE, BBR_PENDING, BBR_DONE };

struct BbReport {
  volatile BbReportSlot slot;
  BbReportKind kind;
  uint32_t gen;                 // the server it was meant for, see bambuddyDeviceReset()
  // in
  char     base[80];
  char     key[80];
  char     ip[24];
  char     fw[BB_FIRMWARE_VERSION_MAX + 1];
  bool     scale_ok;
  uint32_t uptime_s;
  float    grams;
  bool     stable;
  int32_t  raw;
  // out
  int      code;
  char     cmd[40];
  int      write_spool_id;
  uint32_t cost_ms;
};

static BbReport      s_job;
static TaskHandle_t  s_task          = nullptr;
static unsigned long s_task_tried_ms = 0;
static bool          s_task_tried    = false;
static uint32_t      s_gen           = 0;

static void runJob(BbReport& j) {
  const uint32_t t0 = millis();
  j.cmd[0] = '\0';
  j.write_spool_id = 0;
  if (j.kind == BBR_HEARTBEAT) {
    // scale_ok, not scale_ready: with no load cell fitted there is no fault to
    // report, and sending false made BamBuddy render the device as a scale
    // that is broken rather than a device that has none. has_scale in the
    // registration is what distinguishes the two.
    j.code = bbHeartbeat(j.base, j.key, /*nfc_ok=*/true, j.scale_ok, j.uptime_s,
                         j.ip, j.fw, j.cmd, sizeof(j.cmd), &j.write_spool_id,
                         BB_HEARTBEAT_TIMEOUT_MS);
  } else {
    j.code = bbScaleReading(j.base, j.key, j.grams, j.stable, j.raw, BB_WEIGHT_TIMEOUT_MS);
  }
  j.cost_ms = millis() - t0;
}

static void reportTask(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (s_job.slot != BBR_PENDING) continue;
    runJob(s_job);
    __sync_synchronize();
    s_job.slot = BBR_DONE;
  }
}

// Wurzel: the reports ran on the loop task with 4 and 5 s timeouts, and a
// BamBuddy that did not answer stopped touch and drawing for that long.
// Fix: a task of its own runs them (AreYouThereAndi2, 7abb09a), with its
// stack in PSRAM; without PSRAM, or when no block is free, the request runs
// right here as it always did, with the same short timeouts.
// NOT-TO-DO: the stack in internal RAM, as in the original: 12 kB for the
// device's lifetime that the 7B does not have (BB_TASK_STACK_BYTES). Nor
// counting the stack in workerFreeHeap(): it takes no internal RAM, and the
// floors of the other workers would sink by 12 kB for nothing.
// See test_fachfrage_wiring.py (a03) and test_bambuddy_report_rules.py.
static void submitJob() {
  s_job.slot = BBR_PENDING;
  if (!s_task && (!s_task_tried || millis() - s_task_tried_ms >= BB_TASK_RETRY_MS)) {
    const bool first = !s_task_tried;
    s_task_tried    = true;
    s_task_tried_ms = millis();
    if (workerStackCanGoOutside(BB_TASK_STACK_BYTES) &&
        xTaskCreatePinnedToCoreWithCaps(reportTask, "bbreport", BB_TASK_STACK_BYTES, nullptr,
                                        BB_TASK_PRIORITY, &s_task, BB_TASK_CORE,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
      logSDf("BamBuddy: reports go out from a task of their own (%u B stack in PSRAM), "
             "internal heap %u", (unsigned)BB_TASK_STACK_BYTES,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    } else {
      s_task = nullptr;
      if (first) logSD("BamBuddy: no PSRAM for the report task, reports stay in the loop");
    }
  }
  if (s_task) {
    xTaskNotifyGive(s_task);
    return;
  }
  runJob(s_job);
  s_job.slot = BBR_DONE;
}

// The version BamBuddy is told, in registration and heartbeat. Its schema
// refuses more than BB_FIRMWARE_VERSION_MAX characters with HTTP 422, which the
// acceptance firmware ("v0.8.6-beta.14-abnahme", 22) ran into on the scale:
// never registered, heartbeat failing, no command could reach it. So BamBuddy
// gets the release version when the whole one is too long
// (services/bambuddy_device_rules.h); everything else keeps showing FW_VERSION.
static const char* firmwareForBamBuddy() {
  static char sent[BB_FIRMWARE_VERSION_MAX + 1];
  static bool ready = false;
  if (!ready) {
    ready = true;
    if (bbFirmwareVersion(FW_VERSION, SSC_VERSION_SUFFIX, sent, sizeof(sent))) {
      logSDf("BamBuddy: firmware version sent as %s (BamBuddy takes %d characters)",
             sent, BB_FIRMWARE_VERSION_MAX);
    }
  }
  return sent;
}

void bambuddyDeviceReset() {
  s_registered = false;
  s_last_register_ms = 0;
  s_reported_tag[0] = '\0';
  // An answer still on its way was meant for the old server: it is dropped
  // when it arrives (s_gen), and the spacing starts over.
  s_gen++;
  s_hb_gap_ms     = BB_HEARTBEAT_MS;
  s_weight_gap_ms = BB_WEIGHT_MIN_GAP_MS;
  if (s_job.slot == BBR_DONE) s_job.slot = BBR_IDLE;
}

// Answers a command the scale cannot carry out. Every queued command must be
// answered: BamBuddy clears it on the reply, and an unanswered one leaves its
// interface waiting for a device that will never respond.
static void declineCommand(const char* base, const char* key, const char* cmd) {
  bbCommandResult(base, key, cmd, false,
                  "Not supported on SpoolmanScale, this is an ESP32 scale");
  logSDf("BamBuddy: declined command '%s'", cmd);
}

static void handleCommand(const char* base, const char* key, const char* cmd,
                          int write_spool_id) {
  if (strcmp(cmd, "tare") == 0) {
    if (!scale_ready) {
      bbCommandResult(base, key, cmd, false, "Scale not ready");
      logSD("BamBuddy: tare requested but the scale is not ready");
      return;
    }
    // Same three steps the tare button on the main screen performs. The
    // display picks the new value up on the next updateDisplay().
    int32_t raw = scaleHardwareReadRaw();
    saveTareOffset(raw);
    scale_weight_g = 0.0f;
    resetScaleFilter();
    bbSetTare(base, key, raw);
    logSDf("BamBuddy: tare from the server applied (raw=%d)", (int)raw);
    return;
  }

  if (strcmp(cmd, "reboot") == 0 || strcmp(cmd, "restart_daemon") == 0) {
    // restart_daemon means "restart the software" on a Pi, which on this
    // device is the same thing as a reboot.
    bbCommandResult(base, key, cmd, true, "Restarting");
    logSDf("BamBuddy: restarting on request ('%s')", cmd);
    delay(200);          // give the reply a chance to leave
    ESP.restart();
    return;
  }

  if (strcmp(cmd, "write_tag") == 0) {
    // The scale writes tags only from its own screens, where the user confirms
    // at the device with the tag in view - not on a request from elsewhere.
    // Declined by id so BamBuddy can close the dialog it opened rather than
    // sit on "waiting for SpoolBuddy".
    if (write_spool_id > 0) {
      bbWriteTagResult(base, key, write_spool_id, nullptr, false,
                       "SpoolmanScale writes tags from its own screens only");
    } else {
      bbCommandResult(base, key, cmd, false, "SpoolmanScale does not write tags on request");
    }
    logSD("BamBuddy: declined a tag write");
    return;
  }

  if (strncmp(cmd, "run_", 4) == 0) {
    // run_nfc_diag, run_scale_diag, run_read_tag_diag - all of them run a
    // Python script on the Pi. Reported through the diagnostics endpoint
    // rather than the command one, which is where BamBuddy reads the result.
    const char* diag = (strcmp(cmd, "run_scale_diag") == 0)    ? "scale"
                     : (strcmp(cmd, "run_read_tag_diag") == 0) ? "read_tag"
                                                               : "nfc";
    bbDiagnosticResult(base, key, diag, false,
                       "Diagnostics are a Raspberry Pi feature. This is an ESP32 "
                       "scale: check the SD card log on the device instead.", -1);
    logSDf("BamBuddy: declined diagnostic '%s'", diag);
    return;
  }

  // shutdown, restart_browser, apply_system_config and anything a future
  // BamBuddy adds. Declining by name beats silence.
  declineCommand(base, key, cmd);
}

void bambuddyDeviceTick() {
  if (!backendIsBamBuddy() || !wifi_ok) return;

  const char* base = backendBaseUrl();
  if (strlen(base) <= 7) return;            // longer than "http://"
  const char* key = bambuddyApiKey();

  // --- registration -------------------------------------------
  if (!s_registered) {
    if (s_last_register_ms != 0 && millis() - s_last_register_ms < BB_REGISTER_RETRY_MS) {
      return;
    }
    s_last_register_ms = millis();
    int code = bbRegisterDevice(base, key,
                                wifiManagerLocalIP().toString().c_str(),
                                firmwareForBamBuddy(), zero_offset, cal_factor);
    if (code != 200) {
      logSDf("BamBuddy: registration failed (HTTP %d), retrying in %d s",
             code, BB_REGISTER_RETRY_MS / 1000);
      return;
    }
    s_registered = true;
    s_last_hb_ms = millis();               // the registration counts as one
    s_last_hb_ok = true;
  }

  // --- answers from the report task ---------------------------
  if (s_job.slot == BBR_DONE) {
    __sync_synchronize();
    const BbReportKind kind = s_job.kind;
    const bool     current = (s_job.gen == s_gen);
    const int      code    = s_job.code;
    const uint32_t cost    = s_job.cost_ms;
    char cmd[40];
    strncpy(cmd, s_job.cmd, sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';
    const int write_spool_id = s_job.write_spool_id;
    s_job.slot = BBR_IDLE;

    if (!current) {
      // For a server no longer in use: nothing in it applies here.
    } else if (kind == BBR_HEARTBEAT) {
      // Failed (nobody answered): ask less often while that lasts.
      s_hb_gap_ms = bbHeartbeatGapMs(code, s_hb_gap_ms);
      if (code == 404) {
        // The device was removed in the web interface. Registering again is
        // the documented way back, its own daemon does the same.
        logSD("BamBuddy: device is gone from the server, registering again");
        s_registered = false;
        s_last_register_ms = 0;
        return;
      }
      const bool ok = (code == 200);
      if (ok != s_last_hb_ok) {
        // Only on change. A line every ten seconds would bury everything else.
        logSDf("BamBuddy: heartbeat %s (HTTP %d), next in %u s", ok ? "OK" : "FAILED", code,
               (unsigned)(s_hb_gap_ms / 1000));
        s_last_hb_ok = ok;
      }
      if (ok && cmd[0]) handleCommand(base, key, cmd, write_spool_id);
    } else {
      // Spaced by what it cost; a failure doubles the wait, an answer returns
      // to the cost.
      const uint32_t was = s_weight_gap_ms;
      s_weight_gap_ms = bbWeightGapMs(code, cost, s_weight_gap_ms);
      if (code != 200 && s_weight_gap_ms != was) {
        logSDf("BamBuddy: weight report failed (HTTP %d), next in %u ms", code,
               (unsigned)s_weight_gap_ms);
      }
    }
  }

  // --- heartbeat ----------------------------------------------
  if (s_job.slot == BBR_IDLE && millis() - s_last_hb_ms >= s_hb_gap_ms) {
    s_last_hb_ms = millis();
    s_job.kind = BBR_HEARTBEAT;
    s_job.gen  = s_gen;
    snprintf(s_job.base, sizeof(s_job.base), "%s", base);
    snprintf(s_job.key, sizeof(s_job.key), "%s", key ? key : "");
    snprintf(s_job.ip, sizeof(s_job.ip), "%s", wifiManagerLocalIP().toString().c_str());
    snprintf(s_job.fw, sizeof(s_job.fw), "%s", firmwareForBamBuddy());
    s_job.scale_ok = (!g_scale_fitted || scale_ready);
    s_job.uptime_s = millis() / 1000;
    submitJob();
  }

  // --- tag removal --------------------------------------------
  // Only the removal is reported here. The scan itself already goes out as
  // part of the lookup in bbFindSpoolByTag, once per placement, which is
  // exactly the event BamBuddy wants - sending it a second time from here
  // would double every scan.
  if (tag_present && g_tag.uid_str[0]) {
    strncpy(s_reported_tag, g_tag.uid_str, sizeof(s_reported_tag) - 1);
    s_reported_tag[sizeof(s_reported_tag) - 1] = '\0';
  } else if (!tag_present && s_reported_tag[0]) {
    bbTagRemoved(base, key, s_reported_tag);
    s_reported_tag[0] = '\0';
  }

  // --- live weight --------------------------------------------
  if (scale_ready) {
    // Collected on every pass, not only when a report goes out: the spread
    // over the whole interval is what says whether the load has settled.
    if (scale_weight_g < s_win_min) s_win_min = scale_weight_g;
    if (scale_weight_g > s_win_max) s_win_max = scale_weight_g;

    if (s_job.slot == BBR_IDLE && millis() - s_last_weight_ms >= s_weight_gap_ms) {
      if (fabsf(scale_weight_g - s_last_weight_g) >= BB_WEIGHT_MIN_DELTA_G) {
        // Settled means the readings of the past second stayed inside the
        // same window the automatic weighing waits for. Reporting it lets
        // the web interface distinguish a spool being placed from one that
        // is lying still.
        s_job.kind   = BBR_WEIGHT;
        s_job.gen    = s_gen;
        snprintf(s_job.base, sizeof(s_job.base), "%s", base);
        snprintf(s_job.key, sizeof(s_job.key), "%s", key ? key : "");
        s_job.grams  = scale_weight_g;
        s_job.stable = (s_win_max - s_win_min) < AUTO_WEIGHT_THRESH_G;
        s_job.raw    = scaleHardwareReadRaw();
        s_last_weight_g = scale_weight_g;
        submitJob();
      }
      s_last_weight_ms = millis();
      s_win_min =  1e9f;
      s_win_max = -1e9f;
    }
  }
}
