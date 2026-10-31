#include "dried_batch.h"

#include <Arduino.h>
#include <string.h>

#include "app/app_state.h"
#include "hardware/sd_logger.h"
#include "services/backend.h"
#include "services/backend_api.h"
#include "services/backend_job_rules.h"
#include "services/worker_task.h"

// The numbers the web jobs run with, for the same reasons: an https backend
// does its handshake on this stack, the task sits below the loop so the
// display keeps its share, and it lives on the other core. The heap floor is
// checked before the stack is taken from it, and is not a number here: 50000
// at https, 40000 at http (backend_job_rules.h).
#define DRIED_BATCH_STACK_BYTES  16384
#define DRIED_BATCH_PRIORITY     1
#define DRIED_BATCH_CORE         0
// Per spool, the same the single write on the card waits.
#define DRIED_BATCH_TIMEOUT_MS   5000

static volatile DriedBatchState s_state = DBS_IDLE;
static volatile bool            s_abort = false;
static DriedBatchResult         s_res;
// The Spoolman address as it stood when the batch started. Copied because
// the settings screen writes cfg_spoolman_base in place.
static char                     s_base[sizeof(cfg_spoolman_base)] = "";
// The gates of the "not started" line and of the "task creation failed" line,
// one each; only the loop starts a batch.
static LogGate                  s_start_gate;
static LogGate                  s_create_gate;

static void driedBatchTask(void* arg) {
  (void)arg;
  for (uint8_t i = 0; i < s_res.count; i++) {
    if (s_abort) {
      s_res.code[i] = DRIED_BATCH_NOT_RUN;
      continue;
    }
    const int code = backendPatchSpoolLastDried(s_base, s_res.spool_id[i], s_res.iso,
                                                DRIED_BATCH_TIMEOUT_MS);
    s_res.code[i] = code;
    if (backendWriteOk(code)) s_res.ok++;
    logSDf("DRIEDBATCH: spool %d -> HTTP %d", s_res.spool_id[i], code);
  }
  logSDf("DRIEDBATCH: %u of %u written, stack left %u", (unsigned)s_res.ok,
         (unsigned)s_res.count, (unsigned)uxTaskGetStackHighWaterMark(NULL));
  // Everything above is written before the state says so: the loop on the
  // other core reads the state first and the result after.
  __sync_synchronize();
  s_state = DBS_DONE;
  vTaskDelete(NULL);
}

bool driedBatchStart(int printer_id, uint8_t ams_id, const int* spool_ids,
                     uint8_t n, const char* iso) {
  if (s_state != DBS_IDLE || !spool_ids || n == 0 || !iso || !iso[0]) return false;
  // Checked here rather than inside the task: the stack comes out of the
  // heap the moment the task is created, so testing afterwards would be
  // testing the wrong number.
  // The batch writes to the active backend and, for a BamBuddy that keeps its
  // spools in a Spoolman server, straight to that server
  // (backendPatchSpoolLastDried). backendFdbBaseUrl() is that server exactly
  // then and the active backend otherwise: the higher floor of the two.
  const uint32_t min_heap  = backendJobMinHeapBoth(backendBaseUrl(), backendFdbBaseUrl());
  const uint32_t free_heap = workerFreeHeap();
  if (free_heap < min_heap) {
    // One line a minute, with the refusals it stands for.
    if (logGateDue(&s_start_gate, millis(), BACKEND_JOB_REFUSAL_LOG_MS)) {
      logSDf("DRIEDBATCH: not started, heap %u < %u (%u more refusals since the last line)",
             (unsigned)free_heap, (unsigned)min_heap,
             (unsigned)logGateTakeSuppressed(&s_start_gate));
    }
    return false;
  }

  s_res = DriedBatchResult{};
  if (n > DRIED_BATCH_MAX) n = DRIED_BATCH_MAX;
  for (uint8_t i = 0; i < n; i++) s_res.spool_id[i] = spool_ids[i];
  s_res.count      = n;
  s_res.printer_id = printer_id;
  s_res.ams_id     = ams_id;
  snprintf(s_res.iso, sizeof(s_res.iso), "%s", iso);
  snprintf(s_base, sizeof(s_base), "%s", cfg_spoolman_base);
  s_abort = false;

  // Set before the task exists: if creation fails it is cleared again below,
  // and if it succeeds the task may finish before this line would run - in
  // the simulator it always does, the task runs inline there.
  s_state = DBS_RUNNING;
  BaseType_t ok = xTaskCreatePinnedToCore(driedBatchTask, "driedbatch",
                                          DRIED_BATCH_STACK_BYTES, nullptr,
                                          DRIED_BATCH_PRIORITY, nullptr,
                                          DRIED_BATCH_CORE);
  if (ok != pdPASS) {
    s_state = DBS_IDLE;
    // A task that cannot be created is asked for again like a refused start:
    // one line a minute here as well.
    if (logGateDue(&s_create_gate, millis(), BACKEND_JOB_REFUSAL_LOG_MS)) {
      logSDf("DRIEDBATCH: task creation failed (%u more since the last line)",
             (unsigned)logGateTakeSuppressed(&s_create_gate));
    }
    return false;
  }
  // Started: what was refused or failed before is over, the next one gets a line.
  logGateReset(&s_start_gate);
  logGateReset(&s_create_gate);
  return true;
}

DriedBatchState driedBatchState() { return s_state; }

bool driedBatchBusy() { return s_state != DBS_IDLE; }

// The ids are written before the state leaves IDLE and never again while it
// is not, so reading them here from the loop is safe.
bool driedBatchContains(int spool_id) {
  if (spool_id <= 0 || s_state == DBS_IDLE) return false;
  for (uint8_t i = 0; i < s_res.count; i++) {
    if (s_res.spool_id[i] == spool_id) return true;
  }
  return false;
}

const DriedBatchResult& driedBatchResult() { return s_res; }

void driedBatchTake() {
  if (s_state != DBS_DONE) return;
  s_state = DBS_IDLE;
}

void driedBatchCancel() {
  if (s_state == DBS_RUNNING) {
    s_abort = true;
    logSD("DRIEDBATCH: cancelled, remaining spools skipped");
  }
}
