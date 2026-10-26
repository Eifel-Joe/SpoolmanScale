#include "update_check.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include <time.h>
#include <string.h>

#include "app_config.h"
#include "app/app_state.h"
#include "hardware/sd_logger.h"
#include "boards/board.h"
#include "services/backend_job.h"
#include "services/backend_job_rules.h"
#include "services/partition_layout.h"
#include "services/github_release.h"
#include "services/heap_low.h"
#include "services/ota_state.h"
#include "web/web_jobs.h"
#include "web/web_server.h"
#include "services/prefs_store.h"
#include "services/version_compare.h"
#include "services/version_file.h"
#include "ui/ota_github.h"
#include "ui/update_badges.h"

namespace {

// Served from our GitHub Pages (branch gh-pages of the fork, FW_PAGES_URL)
// next to the web flasher and written by the release workflow; the check by
// hand reads the same file (version_file.h). Deliberately not api.github.com:
// that endpoint allows 60 requests per hour and IP, which users behind
// carrier grade NAT share, and its answer for the release list is over fifty
// kilobytes. This file is about a kilobyte and has no rate limit.
constexpr const char* VERSION_URL = VERSION_FILE_URL;

// The first check waits until the boot is long over. Display, NFC, scale and
// the backend are all up well before this, and the heap has settled.
constexpr unsigned long FIRST_RUN_DELAY_MS = 90000;
// Devices in one workshop all boot when the power strip goes on. Without a
// spread they would hit the server in the same second.
constexpr unsigned long FIRST_RUN_JITTER_MS = 60000;
// After a failure, and as the re-evaluation interval while a check is not yet
// due. Long enough that a server outage cannot turn into a request storm.
constexpr unsigned long RETRY_DELAY_MS = 3600000;
// At most one check per day per device.
constexpr uint32_t CHECK_INTERVAL_S = 86400;
constexpr int HTTP_TIMEOUT_MS = 4000;

// Unix time safely in the past. getLocalTime() reports 1970 until NTP has
// answered, and a timestamp from then would make the daily window meaningless.
constexpr uint32_t TIME_IS_SYNCED = 1700000000;

// The mbedTLS handshake is the deepest thing this task does and wants roughly
// 8 kB on its own. 12 kB leaves headroom for the JSON parse on top of it. The
// stack is taken from the heap when the task starts and given back when it
// deletes itself, so this costs nothing for the other 86399 seconds of the day.
// The watermark is logged below; if it stays comfortable this can come down.
// Raised from 12 kB when the connection stopped being setInsecure(): the
// certificate bundle parses the matching root inside the handshake, on this
// stack.
constexpr uint32_t TASK_STACK_BYTES = 14336;
constexpr int      TASK_PRIORITY    = 1;   // below the Arduino loop task
constexpr int      TASK_CORE        = 0;   // the loop runs on core 1

enum Result : uint8_t {
  RES_NONE = 0,
  RES_UP_TO_DATE,
  RES_AVAILABLE,
  RES_FAIL_HTTP,
  RES_FAIL_JSON,
  RES_FAIL_HEAP,
  RES_FAIL_BEGIN,
};

// Handover from the task to updateCheckTick(). Everything else is written
// before s_result and read after it, so the flag alone orders the exchange and
// no mutex is needed.
volatile uint8_t s_result = RES_NONE;
volatile int     s_http_code = 0;
volatile bool    s_task_running = false;
volatile uint32_t s_persist_epoch = 0;   // non-zero: tick writes it to NVS
// The lowest the internal heap went during the request (heap_low.h), 0 when it
// could not be measured. Set before s_result on every way out of the request,
// for every line of a check that ran.
volatile uint32_t s_check_low = 0;

// What maybeStartCheck() found when it turned the check down for the heap,
// for the log line of RES_FAIL_HEAP. Both are written and read on the loop
// task; the task of the check does not exist then.
uint32_t s_skip_free  = 0;
uint32_t s_skip_floor = 0;

// The internal heap when the check was started, the other half of the line the
// tick writes when the task is done: where it started, how low it went.
// Written on the loop task before the task exists and read on it after the
// result, so no ordering is needed beyond that.
uint32_t s_heap_at_start = 0;

// The task stages the tag it found here instead of writing gh_latest_version
// directly. That buffer is read by the OTA screen on the loop task, and a
// background result landing mid-draw would otherwise show a half-written
// string. Only updateCheckTick() copies it across.
char s_found_version[32] = "";
char s_found_sha[65]     = "";
// The image's size, next to its checksum in version.json; 0 from a file
// written before the workflow published it.
volatile uint32_t s_found_size = 0;

unsigned long s_due_ms = 0;
bool s_scheduled = false;

// The next answer was asked for on the OTA screen (updateCheckScheduleIn) and
// goes there too, see otaGithubShowCheckAnswer(). Loop task only, like the
// schedule; taken back with the answer, so a later daily check is not shown.
bool s_answer_to_screen = false;

// The request itself, in a function of its own that returns. It used to be the
// body of updateCheckTask(), which ends with vTaskDelete(NULL): that call does
// not return, so the destructors of the client, the connection and the document
// declared there never ran. The WiFiClientSecure frees its ssl context only in
// its destructor, the HTTPClient keeps its socket open after end() for reuse
// and stops it only there, and the document keeps its pool: 4-5 kB of internal
// heap per check, and on the 7B (57 kB free, floor 50 kB) the check locked
// itself out after about two runs. Freeing them by hand on every way out was
// rejected: client.stop() does not free the ssl context, and the next local
// someone adds would leak again. test/test_worker_heap_floors.py holds every
// self-deleting task in src to this.
static void runUpdateCheck() {
  WiFiClientSecure client;
  githubTrust(client);
  HTTPClient http;

  // The low point of this request, for the line the tick writes. Every way out
  // of the request ends it: the monitor is global, and one left running would
  // make the next check's start fail.
  heapLowBegin();
  if (!http.begin(client, VERSION_URL)) {
    s_check_low = heapLowEnd();
    s_result = RES_FAIL_BEGIN;
    return;
  }

  http.addHeader("User-Agent", "SpoolmanScale-ESP32");
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);

  int code = http.GET();
  s_http_code = code;

  if (code != 200) {
    Serial.printf("[upd] HTTP %d\n", code);
    http.end();
    // A failed answer went through the same handshake as a good one, so its
    // line carries the low point too; handed over before the result.
    s_check_low = heapLowEnd();
    s_result = RES_FAIL_HTTP;
    return;
  }

  // Parsed straight off the stream. The old code pulled the whole body into a
  // String first, which for the release list meant a six figure allocation on
  // a fragmented heap.
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  // Before any s_result below, like the other values the tick reads. The parse
  // above is inside the window: it runs on this task's stack and heap too.
  s_check_low = heapLowEnd();

  if (err) {
    Serial.printf("[upd] JSON error: %s\n", err.c_str());
    s_result = RES_FAIL_JSON;
    return;
  }

  // The release this board takes, by the rule the check by hand follows too
  // (version_file.h): its own node or the first board's top level, the
  // pre-release when asked for and there, else the stable one.
  VersionPick pick;
  const VersionFileVerdict verdict = versionFilePick(doc.as<JsonVariantConst>(), gh_prerelease, &pick);
  s_found_size = pick.size;

  if (verdict != VF_RELEASE) {
    // Nothing published for this board yet is an answer, not a broken file.
    if (verdict == VF_NONE_FOR_BOARD) {
      Serial.println("[upd] version.json has no release for " BOARD_ID);
      time_t checked = time(nullptr);
      if ((uint32_t)checked > TIME_IS_SYNCED) s_persist_epoch = (uint32_t)checked;
      s_result = RES_UP_TO_DATE;
      return;
    }
    s_result = RES_FAIL_JSON;
    return;
  }

  strncpy(s_found_version, pick.tag, sizeof(s_found_version) - 1);
  s_found_version[sizeof(s_found_version) - 1] = '\0';
  // Published next to the tag by the release workflow. A version.json from
  // before that carries none (""), and the flash then runs on TLS alone.
  strncpy(s_found_sha, pick.sha, sizeof(s_found_sha) - 1);
  s_found_sha[sizeof(s_found_sha) - 1] = '\0';

  uint64_t installed = parseVersion(FW_VERSION);
  uint64_t remote    = parseVersion(s_found_version);
  Serial.printf("[upd] installed=%s latest=%s\n", FW_VERSION, s_found_version);

  Serial.printf("[upd] task stack low water mark: %u bytes free\n",
                (unsigned)uxTaskGetStackHighWaterMark(NULL));

  // The server answered, so the daily window starts now. Set before s_result:
  // the tick reads the result first and would otherwise take the no-clock
  // branch and lose the timestamp until its next pass.
  time_t now = time(nullptr);
  if ((uint32_t)now > TIME_IS_SYNCED) s_persist_epoch = (uint32_t)now;

  // Published last. Everything the tick needs is in place by now.
  s_result = (remote > installed) ? RES_AVAILABLE : RES_UP_TO_DATE;
}

void updateCheckTask(void* arg) {
  (void)arg;
  runUpdateCheck();
  // Only now, with the client, the connection and the document destroyed. The
  // guard keeps the web jobs and the next check from starting while they still
  // hold their memory, so it drops after them and not with the result.
  s_task_running = false;
  vTaskDelete(NULL);
}

void maybeStartCheck() {
  if (!s_scheduled) return;
  if (!g_upd_autocheck) return;
  if (!wifi_ok) return;
  if (s_task_running) return;
  // Never a second TLS connection while an image is being written.
  if (gh_flash_active || otaWebUploadActive()) return;
  // Nor beside a web job (the GitHub check, the release notes, the colour
  // table, the spool list) or the inventory list. The floor below is for one
  // background request: its stack and its connection take ~22-24 kB of the
  // internal heap, and a second pair would take what the floor leaves free.
  // Not logged and not postponed, the due time stays as it is: the other one
  // is over in seconds and the next pass asks again. The web jobs wait for
  // this check the same way (webJobStart). "Check" on the scale waits only
  // for this check (doGithubOtaCheck), not for the web jobs; its deferred
  // retry in the loop (app_loop.cpp) waits for both.
  if (webJobState() == WJS_RUNNING || backendJobState() == BJS_RUNNING) return;
  // Signed difference, so this survives the millis() wrap after 49 days.
  if ((long)(millis() - s_due_ms) < 0) return;

  // Checked here rather than inside the task: the 14 kB stack comes out of the
  // heap the moment the task is created, so testing after the fact would be
  // testing the wrong number. Skipping a version check costs nothing, an out
  // of memory reboot costs the user their session. The floor is the rule every
  // https worker asks, not a number of this file: the request goes to GitHub,
  // so it is https, and TLS itself lives in PSRAM here (the measurements are
  // in backend_job_rules.h). It used to be 60000 for a handshake of 40 kB that
  // this firmware does not take from the internal heap, which kept the 7B
  // (57 kB free in operation) from running the daily check.
  const uint32_t min_heap  = backendJobMinHeap(VERSION_URL);
  const uint32_t free_heap = (uint32_t)ESP.getFreeHeap();
  if (free_heap < min_heap) {
    Serial.printf("[upd] postponed, heap %u < %u\n", (unsigned)free_heap, (unsigned)min_heap);
    // For the line updateCheckTick() writes: what was free, what was needed.
    s_skip_free  = free_heap;
    s_skip_floor = min_heap;
    s_result = RES_FAIL_HEAP;
    s_due_ms = millis() + RETRY_DELAY_MS;
    return;
  }

  // Without a synced clock this test cannot run; updateCheckTick() then keeps
  // the interval on uptime instead.
  time_t now = time(nullptr);
  if ((uint32_t)now > TIME_IS_SYNCED && g_upd_last_epoch > TIME_IS_SYNCED &&
      (uint32_t)now - g_upd_last_epoch < CHECK_INTERVAL_S) {
    s_due_ms = millis() + RETRY_DELAY_MS;
    return;
  }

  // Set before the task exists: if creation fails the guard is cleared again
  // below, and if it succeeds the task may finish before this line would run.
  s_task_running = true;
  s_due_ms = millis() + RETRY_DELAY_MS;
  // What the task starts from. Nothing allocates between the floor test above
  // and here, and the stack comes out of this number the moment the task exists.
  s_heap_at_start = free_heap;

  BaseType_t ok = xTaskCreatePinnedToCore(updateCheckTask, "updchk",
                                          TASK_STACK_BYTES, nullptr,
                                          TASK_PRIORITY, nullptr, TASK_CORE);
  if (ok != pdPASS) {
    s_task_running = false;
    Serial.println("[upd] task creation failed");
  }
}

}  // namespace

bool updateCheckBusy() { return s_task_running; }

// Restores the badge from what the last check found, without going near the
// network. Comparing rather than trusting a stored flag means it corrects
// itself: once FW_VERSION has caught up, the badge stays off on its own.
void updateCheckRestoreBadge() {
  String stored_s = prefsGetString("upd_ver", "");
  const char* stored = stored_s.c_str();
  if (stored[0] == '\0') return;
  if (parseVersion(stored) <= parseVersion(FW_VERSION)) return;

  // With the checksum that came with it, if the workflow published one.
  String sha_s = prefsGetString("upd_sha", "");
  otaSetLatest(stored, sha_s.c_str());
  update_available = true;
  showUpdateBadges(true);
  logSDf("Update badge restored from NVS: %s > %s", stored, FW_VERSION);
}

void updateCheckScheduleFirstRun() {
  s_due_ms = millis() + FIRST_RUN_DELAY_MS + (esp_random() % FIRST_RUN_JITTER_MS);
  s_scheduled = true;
}

void updateCheckScheduleIn(unsigned long delay_ms) {
  s_due_ms = millis() + delay_ms;
  s_scheduled = true;
  s_answer_to_screen = true;
}

void updateCheckTick() {
  // Everything below runs on the loop task on purpose. LVGL is not thread
  // safe, the SD logger shares one file handle, and NVS writes block on flash.
  // The task only sets flags; the side effects happen here.
  uint8_t result = s_result;
  bool answered = (result == RES_AVAILABLE || result == RES_UP_TO_DATE);
  if (result != RES_NONE) {
    s_result = RES_NONE;
    // Taken with the answer it belongs to: a check the OTA screen asked for
    // shows its answer there (s_answer_to_screen), the next daily one does not.
    const bool to_screen = s_answer_to_screen;
    s_answer_to_screen = false;
    GhCheckAnswer shown = GH_ANSWER_FAILED;
    // For every line of a check that ran (all but the skipped one, which
    // started no request); 0 reads n/a.
    char low_txt[12];
    heapLowText(s_check_low, low_txt, sizeof(low_txt));
    switch (result) {
      case RES_AVAILABLE:
        // Larger than this device's app slot: no badge that leads to a
        // download bound to fail, the hint with the way to the flasher instead.
        // partitionNoteTooBig() says so once per version and boot; the line of
        // the check is written every time, like the others.
        if (!partitionImageFits(s_found_size)) {
          partitionNoteTooBig(s_found_version, s_found_size);
          logSDf("Update check: %s too big for this slot (installed %s), internal heap %u at start, low %s",
                 s_found_version, FW_VERSION, (unsigned)s_heap_at_start, low_txt);
          shown = GH_ANSWER_TOO_BIG;
          break;
        }
        otaSetLatest(s_found_version, s_found_sha);
        update_available = true;
        showUpdateBadges(true);
        logSDf("Update check: %s available (installed %s), internal heap %u at start, low %s",
               gh_latest_version, FW_VERSION, (unsigned)s_heap_at_start, low_txt);
        shown = GH_ANSWER_AVAILABLE;
        break;
      case RES_UP_TO_DATE:
        // A clean result has to take the badge down again, not just leave the
        // old one standing. Switching pre-releases off would otherwise keep
        // the dot for a build the device no longer looks for, while the OTA
        // screen showed no version to install.
        //
        // Not while that screen is open, though: the user may be looking at
        // the result of a manual check, and pulling the labels out from under
        // it would be the device arguing with itself. The next check settles it.
        // Unless the screen asked for this answer itself: then it is the
        // result the user is waiting for, and the screen shows it below.
        if (!otaGithubScreenVisible() || to_screen) {
          otaSetLatest("", "");
          update_available = false;
          showUpdateBadges(false);
        }
        // Always, not only in verbose mode: the low point of every check is
        // what the start floor is measured against, and once a day is no noise.
        logSDf("Update check: up to date (%s), internal heap %u at start, low %s",
               FW_VERSION, (unsigned)s_heap_at_start, low_txt);
        shown = GH_ANSWER_UP_TO_DATE;
        break;
      case RES_FAIL_HTTP:
        logSDf("Update check: HTTP %d, internal heap %u at start, low %s",
               s_http_code, (unsigned)s_heap_at_start, low_txt);
        break;
      case RES_FAIL_JSON:
        logSDf("Update check: bad response, internal heap %u at start, low %s",
               (unsigned)s_heap_at_start, low_txt);
        break;
      case RES_FAIL_HEAP:
        logSDf("Update check: skipped, heap %u < %u", (unsigned)s_skip_free, (unsigned)s_skip_floor);
        break;
      case RES_FAIL_BEGIN:
        logSDf("Update check: connection setup failed, internal heap %u at start, low %s",
               (unsigned)s_heap_at_start, low_txt);
        break;
      default:
        break;
    }
    // After the switch, with the stored update and the badges already set:
    // the screen is built again from them.
    if (to_screen) otaGithubShowCheckAnswer(shown, s_found_version);
  }

  uint32_t epoch = s_persist_epoch;
  if (epoch != 0) {
    s_persist_epoch = 0;
    g_upd_last_epoch = epoch;
    prefsPutUInt("upd_last", epoch);
    // The tag goes with it. Only the timestamp used to be kept, and since the
    // check skips itself for a day, a reboot inside that window left the badge
    // off while an update was in fact waiting.
    //
    // Only a version the badge stands for. gh_latest_version also holds one
    // found too large for this device's slot, and stored here it came back
    // after a reboot as "update available" through updateCheckRestoreBadge().
    const bool keep = update_available && !partitionTagTooBig(gh_latest_version);
    prefsPutString("upd_ver", keep ? gh_latest_version : "");
    prefsPutString("upd_sha", keep ? gh_latest_sha : "");
  } else if (answered) {
    // The server answered but NTP has not, so the daily window cannot be
    // measured in wall clock time. Count uptime instead, otherwise the retry
    // interval would take over and the device would check every hour.
    s_due_ms = millis() + (unsigned long)CHECK_INTERVAL_S * 1000UL;
  }

  maybeStartCheck();
}
