#include "backend_job.h"

#include <Arduino.h>

#include "app/app_state.h"
#include "app/backend_switch.h"
#include "hardware/sd_logger.h"
#include "services/backend_api.h"
#include "services/backend_job_rules.h"
#include "services/http_progress.h"
#include "web/web_jobs.h"

// The priority, core and heap floor of the web jobs, for the same reasons: the
// task sits below the loop so the display keeps its share, it lives on the
// other core, and the heap floor is checked before the stack is taken from it.
// The stack is theirs as well. 10 kB was measured over plain http - a list on
// all three backends left 13.6 of 16 kB unused (beta.43, 23.09.2026) - but an
// https backend puts the TLS handshake on this task whenever the kept
// connection is closed, and the warm-up task needs 14 kB for that very
// handshake.
// The floor is not one number: 60000 for https, for that handshake, and 40000
// for plain http (backend_job_rules.h). The Waveshare 7B has about 58 620
// bytes free in steady state, which the old 60000 for everybody never cleared,
// so an unknown tag was never looked up on a Bambuddy over http.
#define BACKEND_JOB_STACK_BYTES   16384
#define BACKEND_JOB_PRIORITY      1
#define BACKEND_JOB_CORE          0

// The same allocator the lookup path uses: the inventory goes into PSRAM,
// not into the 320 kB the rest of the firmware shares. Static, because the
// document outlives every function that touches it.
struct SpiRamAllocator : ArduinoJson::Allocator {
  void* allocate(size_t n) override {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (!p) p = malloc(n);
    return p;
  }
  void  deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override {
    void* q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM);
    if (!q) q = realloc(p, n);
    return q;
  }
};

static SpiRamAllocator            s_alloc;
static JsonDocument               s_doc(&s_alloc);
static JsonDocument               s_filter;
static bool                       s_has_filter = false;
static volatile BackendJobState   s_state = BJS_IDLE;
static volatile size_t            s_bytes = 0;
static BackendListResult          s_res;
static uint32_t                   s_timeout_ms = 0;
static uint32_t                   s_retry_pause_ms = 0;
// The address as it was when the job started. A host change on the loop
// rewrites cfg_spoolman_base, and this task must not read it half written; the
// generation then drops whatever came back.
static char                       s_base[sizeof(cfg_spoolman_base)] = "";
// The gate of the "postponed" line; only the loop task starts a list.
static LogGate                    s_refusal_gate;
// The internal heap when the task is created: what was free, and the lowest it
// had been since boot. Written before the task exists, read by it when done,
// so that its line shows how deep the list pushed the heap.
static uint32_t                   s_heap_at_start = 0;
static uint32_t                   s_heap_low_before = 0;

static bool transientParseError(const DeserializationError& e) {
  return e == DeserializationError::IncompleteInput ||
         e == DeserializationError::EmptyInput;
}

// The retry rules of the lookup, carried over as they were: another try after
// an HTTP error or a stream that broke off, none after a parse error that
// would only repeat itself.
static void runList() {
  const uint32_t t0 = millis();
  JsonDocument* filter = s_has_filter ? &s_filter : nullptr;
  for (uint8_t attempt = 1; attempt <= s_res.attempts; attempt++) {
    if (attempt > 1) {
      logSDf("Backend job: retry attempt %d (prev err=%s)", attempt, s_res.err.c_str());
      delay(s_retry_pause_ms);     // a task delay on this core, the loop runs on
      s_doc.clear();
      s_bytes = 0;
    }
    s_res.err  = DeserializationError::Ok;
    s_res.code = backendGetSpoolListJson(s_base, s_res.archived, s_doc,
                                         s_timeout_ms, filter, &s_res.err,
                                         s_res.archived_only);
    // Right after the call, before anything else can ask for a list: the flag
    // is the backend layer's, and it is what makes a short list no answer.
    s_res.partial = backendLastListPartial();
    if (s_res.code != 200) {
      logSDf("Backend job: HTTP error %d (attempt %d)", s_res.code, attempt);
      if (attempt == s_res.attempts) { s_res.gave_up = true; break; }
      if (s_res.code == -2 && !transientParseError(s_res.err)) break;
      continue;
    }
    if (!s_res.err) break;
    if (!transientParseError(s_res.err)) break;
  }
  s_res.ms = millis() - t0;
}

static void backendJobTask(void* arg) {
  (void)arg;
  httpCountBytesInto(&s_bytes);
  runList();
  httpCountBytesInto(nullptr);
  logSDf("Backend job: list%s done in %lu ms, code=%d err=%s partial=%d, %u bytes, stack left %u, "
         "heap %u at start, lowest since boot %u (was %u)",
         s_res.archived_only ? " of the archive" : s_res.archived ? " with archive" : "",
         (unsigned long)s_res.ms, s_res.code,
         s_res.err.c_str(), (int)s_res.partial, (unsigned)s_bytes,
         (unsigned)uxTaskGetStackHighWaterMark(NULL),
         (unsigned)s_heap_at_start,
         (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
         (unsigned)s_heap_low_before);
  // Everything above is written before the state says so: the loop on the
  // other core reads the state first and the result after.
  __sync_synchronize();
  s_state = BJS_DONE;
  vTaskDelete(NULL);
}

bool backendJobStartList(bool allow_archived, const JsonDocument* filter,
                         uint32_t timeout_ms, uint8_t attempts,
                         uint32_t retry_pause_ms, bool archived_only) {
  if (backendListBusy() || s_state != BJS_IDLE) return false;
  // Checked here rather than inside the task: the stack comes out of the
  // heap the moment the task is created. The floor follows the address the
  // job is about to copy into s_base below.
  const uint32_t min_heap  = backendJobMinHeap(cfg_spoolman_base);
  const uint32_t free_heap = (uint32_t)ESP.getFreeHeap();
  if (free_heap < min_heap) {
    // The callers ask again on every pass of the loop; a line each time
    // filled the log (over 4000 of them, 25.10.2026), so one a minute.
    if (logGateDue(&s_refusal_gate, millis(), BACKEND_JOB_REFUSAL_LOG_MS)) {
      logSDf("Backend job: postponed, heap %u < %u (%u more refusals since the last line)",
             (unsigned)free_heap, (unsigned)min_heap,
             (unsigned)logGateTakeSuppressed(&s_refusal_gate));
    }
    return false;
  }
  s_doc.clear();
  s_filter.clear();
  s_has_filter = (filter != nullptr);
  if (filter) s_filter.set(filter->as<JsonVariantConst>());
  s_res.code     = 0;
  s_res.err      = DeserializationError::Ok;
  s_res.partial  = false;
  s_res.gave_up  = false;
  s_res.archived = allow_archived;
  s_res.archived_only = archived_only;
  s_res.attempts = attempts ? attempts : 1;
  s_res.gen      = backendGeneration();
  s_res.ms       = 0;
  s_timeout_ms     = timeout_ms;
  s_retry_pause_ms = retry_pause_ms;
  snprintf(s_base, sizeof(s_base), "%s", cfg_spoolman_base);
  s_bytes = 0;
  // Taken last, with everything this function allocated in place and before
  // the task's stack is taken from the heap: the heap the task starts from.
  s_heap_at_start   = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  s_heap_low_before = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);

  // Set before the task exists: if creation fails it is cleared again below,
  // and if it succeeds the task may finish before this line would run - on
  // the simulator it always does.
  s_state = BJS_RUNNING;
  BaseType_t ok = xTaskCreatePinnedToCore(backendJobTask, "backendjob",
                                          BACKEND_JOB_STACK_BYTES, nullptr,
                                          BACKEND_JOB_PRIORITY, nullptr,
                                          BACKEND_JOB_CORE);
  if (ok != pdPASS) {
    s_state = BJS_IDLE;
    logSD("Backend job: task creation failed");
    return false;
  }
  // Started: whatever was refused before is over, and the next refusal gets a
  // line of its own.
  logGateReset(&s_refusal_gate);
  return true;
}

BackendJobState backendJobState() { return s_state; }

bool backendListBusy() {
  return s_state == BJS_RUNNING ||
         (webJobState() == WJS_RUNNING && webJobKind() == WJ_SPOOLS);
}

size_t backendJobBytes() { return s_bytes; }

const BackendListResult& backendJobResult() { return s_res; }
JsonDocument&            backendJobDoc()    { return s_doc; }

void backendJobTake() {
  if (s_state != BJS_DONE) return;
  s_doc.clear();
  // clear() keeps the pool it grew into; shrinking hands the PSRAM back, which
  // the fetch by id after an archived match wants.
  s_doc.shrinkToFit();
  s_filter.clear();
  s_state = BJS_IDLE;
}
