#include "filament_db_store.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <esp_rom_crc.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../bambu/bambu_catalog.h"
#include "../hardware/flash_log.h"
#include "../hardware/sd_logger.h"
#include "backend_job_rules.h"
#include "filament_db.h"
#include "heap_low.h"
#include "worker_task.h"
#include "tag_family.h"

// Behind the log ring, the Bambu catalog and the tag family memory. Until
// beta.50 this sat right on top of the tag family memory, and each overwrote
// the other. 28 kB at the most today
// (400 makers, 640 pairs), in 32 kB so the database can grow. Only an index
// read from the whole database is kept here, SpoolmanDB's 299 pairs; a light
// form answers in a fraction of a second and may hold more than fits.
#define STORE_PAIRS_MAX     640
#define STORE_OFFSET        (FLASH_LOG_BYTES + BAMBU_CATALOG_BYTES + TAG_FAMILY_BYTES)
#define STORE_BYTES         (32UL * 1024UL)
#define STORE_SECTOR        4096UL
#define STORE_MAGIC         0x49424446UL   // "FDBI"
// The records are stored as they lie in memory: a change to FdbMaker or
// FdbPair needs a new version, and a copy of an older one counts as none.
#define STORE_VERSION       1
#define STORE_MAX_AGE_S     (7UL * 24UL * 60UL * 60UL)
// A stamp before this is a clock that was never set, not a date.
#define STORE_STAMP_MIN     1700000000UL
#define STORE_URL_MAX       96

// The write runs on a task of its own (V64), the way the Bambu catalog's does
// (V59): its stack lies in internal RAM, which a task that writes the flash
// needs, and it opens no connection. 4 kB hold esp_partition_* and two log
// lines (logSDf's 256 B); the high-water mark is in the log.
#define FDB_STORE_STACK_BYTES    4096
#define FDB_STORE_PRIORITY       1
#define FDB_STORE_CORE           0
// What a background request leaves of the internal heap at its lowest
// (Spec 8 T: the 50 000 floor less the at most 24 000 of the request). The
// store starts with that and its own stack free, so the rest of the firmware
// keeps as much while the index is written as while it is downloaded.
#define FDB_STORE_RESERVE_BYTES  26000UL
#define FDB_STORE_MIN_HEAP       (FDB_STORE_RESERVE_BYTES + FDB_STORE_STACK_BYTES)
// How long a copy that was handed over waits for that heap. Then it is
// dropped: the next restart reads the 3 MB again, nothing else is lost.
#define FDB_STORE_START_WAIT_MS  60000UL

static_assert(sizeof(FdbMaker) == 36, "a stored maker is 36 bytes; a change needs STORE_VERSION");
static_assert(sizeof(FdbPair) == 22, "a stored pair is 22 bytes; a change needs STORE_VERSION");

struct StoreHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t maker_n;
  uint16_t pair_n;
  uint16_t reserved0;
  uint32_t crc;          // over the makers and the pairs
  uint32_t stamp;        // when the index was read, Unix time
  char     base_url[STORE_URL_MAX];
  uint8_t  reserved[12];
};
static_assert(sizeof(StoreHeader) == 128, "the header is 128 bytes on the device and the host");
static_assert(sizeof(StoreHeader) + FDB_MAKERS_MAX * sizeof(FdbMaker) +
              STORE_PAIRS_MAX * sizeof(FdbPair) <= STORE_BYTES, "a full index fits the region");

static const esp_partition_t* dataPartition() {
  const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                      ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
  if (!p || p->size < STORE_OFFSET + STORE_BYTES) return nullptr;
  return p;
}

static size_t bodyBytes(int maker_n, int pair_n) {
  return (size_t)maker_n * sizeof(FdbMaker) + (size_t)pair_n * sizeof(FdbPair);
}

static uint32_t clockNow() {
  const time_t now = time(nullptr);
  return now >= (time_t)STORE_STAMP_MIN ? (uint32_t)now : 0;
}

// Why a stored header is not used, or nullptr when it is.
static const char* headerProblem(const StoreHeader& h, const char* base_url, uint32_t now) {
  if (h.magic != STORE_MAGIC || h.version != STORE_VERSION) return "none";
  if (h.maker_n == 0 || h.maker_n > FDB_MAKERS_MAX || h.pair_n > STORE_PAIRS_MAX) return "damaged";
  if (strncmp(h.base_url, base_url, sizeof(h.base_url)) != 0) return "another server";
  if (!now) return "clock not set";
  if (h.stamp < STORE_STAMP_MIN || now < h.stamp || now - h.stamp > STORE_MAX_AGE_S) return "too old";
  return nullptr;
}

// Reads len bytes at off into dst through a chunk on the calling task's stack.
// Wurzel: a read straight into PSRAM goes through a bounce buffer of up to
//   16 kB from the internal heap (ESP-IDF 5.5 esp_flash_api.c esp_flash_read(),
//   MAX_READ_CHUNK; spi_flash_os_func_app.c get_buffer_malloc()), the way the
//   Bambu catalog's read-back took the 7B down to 24 128 B (acceptance of
//   beta.19). The stored index, some 20 kB, was read like that in one go.
// Fix: 256 B at a time into memory on the loop's stack, which is internal
//   RAM, so no bounce buffer is taken. As readChunked() in bambu_catalog.cpp.
// Test: test/test_filament_db_store.py.
static bool readChunked(const esp_partition_t* part, size_t off, uint8_t* dst, size_t len) {
  uint8_t chunk[256];
  for (size_t at = 0; at < len; ) {
    const size_t n = len - at < sizeof(chunk) ? len - at : sizeof(chunk);
    if (esp_partition_read(part, off + at, chunk, n) != ESP_OK) return false;
    memcpy(dst + at, chunk, n);
    at += n;
  }
  return true;
}

// ---- reading: on the loop, handed to the task --------------------------
//
// Wurzel: the database task read the stored index and wrote a new one itself,
//   so its 16 kB stack had to lie in internal RAM (a stack in PSRAM must not
//   be in use during a flash access, ESP-IDF 5.5 cache_utils.c line 141), and
//   all three loads of the picker needed the whole floor: 50 000 B against an
//   https server, where the 7B has 49.4 to 50.2 kB in operation. The picker
//   stayed empty and asked again every two seconds (Punkteliste n64).
// Fix (V64): the loop reads the stored index into PSRAM before the task
//   starts, the task puts it in place from memory; a new index goes to a
//   store task with a small internal stack (fdbStoreTick()). The task touches
//   no flash, its stack may go to PSRAM and its floor drops by it.
// NOT-TO-DO: the floor of this one job lowered by hand - the low point during
//   the request would fall under the 26 kB of Spec 8 T. The index kept on
//   the SD card instead - only a scale with a card would gain, and the task
//   would again write on its own, to the card the loop writes the log to.
// Test: test/test_filament_db_store.py, test/test_worker_stack.py (2, 3b).

// Written by the loop before the task exists, read and cleared by the task;
// the loop frees what the task did not take at the next prepare, when the
// last task is long done.
static uint8_t*     s_prep_buf     = nullptr;
static StoreHeader  s_prep_h;
static const char*  s_prep_problem = "none";
static uint32_t     s_prep_ms      = 0;
static uint32_t     s_prep_low     = 0;
static bool         s_forget_once  = false;

void fdbStoreForgetOnce() {
  s_forget_once = true;
}

static const char* const PASSED_OVER = "passed over once for the acceptance";

bool fdbStorePassedOver() {
  return s_prep_problem == PASSED_OVER;
}

bool fdbStorePrepare(const char* base_url) {
  heap_caps_free(s_prep_buf);
  s_prep_buf     = nullptr;
  s_prep_problem = "none";
  if (s_forget_once) {
    s_forget_once  = false;
    s_prep_problem = PASSED_OVER;
    return false;
  }
  const esp_partition_t* part = dataPartition();
  if (!part || !base_url) return false;
  StoreHeader h;
  if (esp_partition_read(part, STORE_OFFSET, &h, sizeof(h)) != ESP_OK) return false;
  const char* problem = headerProblem(h, base_url, clockNow());
  if (problem) { s_prep_problem = problem; return false; }

  const size_t body = bodyBytes(h.maker_n, h.pair_n);
  uint8_t* buf = (uint8_t*)heap_caps_malloc(body, MALLOC_CAP_SPIRAM);
  if (!buf) buf = (uint8_t*)malloc(body);
  if (!buf) { s_prep_problem = "no memory to read it"; return false; }
  // How long the loop held on to the read, and how low the internal heap went
  // meanwhile: the line of the task's load shows both (V64, Spec 8 T).
  const uint32_t t0 = millis();
  heapLowBegin();
  const bool ok = readChunked(part, STORE_OFFSET + sizeof(h), buf, body) &&
                  esp_rom_crc32_le(0, buf, body) == h.crc;
  s_prep_low = heapLowEnd();
  s_prep_ms  = millis() - t0;
  if (!ok) { heap_caps_free(buf); s_prep_problem = "damaged"; return false; }
  s_prep_h   = h;
  s_prep_buf = buf;
  return true;
}

bool fdbStoreLoad(const char* base_url) {
  if (!s_prep_buf) {
    logSDf("Filament DB: stored index not used (%s)", s_prep_problem);
    return false;
  }
  uint8_t* buf = s_prep_buf;
  s_prep_buf = nullptr;
  const StoreHeader& h = s_prep_h;
  bool ok = base_url && strncmp(h.base_url, base_url, sizeof(h.base_url)) == 0;
  if (ok) {
    const FdbMaker* makers = (const FdbMaker*)buf;
    const FdbPair* pairs = (const FdbPair*)(buf + (size_t)h.maker_n * sizeof(FdbMaker));
    ok = fdbIndexRestore(makers, h.maker_n, pairs, h.pair_n);
  }
  heap_caps_free(buf);
  if (!ok) { logSD("Filament DB: stored index damaged, reading the database"); return false; }
  char low_txt[12];
  logSDf("Filament DB: index from flash, %lu h old, %u makers, %u pairs, read on the loop in %lu ms, "
         "internal low %s",
         (unsigned long)((clockNow() - h.stamp) / 3600UL), (unsigned)h.maker_n, (unsigned)h.pair_n,
         (unsigned long)s_prep_ms, heapLowText(s_prep_low, low_txt, sizeof(low_txt)));
  return true;
}

// ---- writing: handed over by the task, written by the store task -------

// The hand-over: the database task builds a copy in PSRAM and sets
// HAND_BUILT; from then on the copy belongs to the loop. The loop moves it to
// HAND_STORING and starts the store task, which sets HAND_STORED or
// HAND_FAILED; the loop deletes the task, frees the copy and sets HAND_IDLE.
// A copy the store never started on is dropped by the loop after
// FDB_STORE_START_WAIT_MS. The task does not wait for the verdict: the
// stored index only spares the next restart its 3 MB, and the picker has the
// index in memory either way.
enum FdbHand : int { HAND_IDLE = 0, HAND_BUILT, HAND_STORING, HAND_STORED, HAND_FAILED };
static volatile int           s_hand       = HAND_IDLE;
static uint8_t* volatile      s_hand_buf   = nullptr;
static volatile size_t        s_hand_len   = 0;
static volatile uint32_t      s_hand_since = 0;
static const esp_partition_t* s_store_part = nullptr;
static WorkerTask             s_store_task = {};
static LogGate                s_store_gate = {};   // the "store postponed" lines

void fdbStoreSave(const char* base_url) {
  const uint32_t now = clockNow();
  // Without a date the age could never be told: better none than one that
  // never runs out.
  if (!base_url || !now) return;
  if (s_hand != HAND_IDLE) {
    logSD("Filament DB: the last index is still being kept, this one is not");
    return;
  }
  const FdbMaker* makers = nullptr;
  const FdbPair* pairs = nullptr;
  int pair_n = 0;
  const int maker_n = fdbIndexSnapshot(&makers, &pairs, &pair_n);
  if (maker_n <= 0) return;
  if (pair_n > STORE_PAIRS_MAX) {
    logSDf("Filament DB: %d pairs do not fit in flash, not kept", pair_n);
    return;
  }

  const size_t maker_bytes = (size_t)maker_n * sizeof(FdbMaker);
  const size_t body  = bodyBytes(maker_n, pair_n);
  const size_t total = sizeof(StoreHeader) + body;
  uint8_t* buf = (uint8_t*)heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
  if (!buf) buf = (uint8_t*)malloc(total);
  if (!buf) { logSD("Filament DB: no memory to keep the index"); return; }
  uint8_t* b = buf + sizeof(StoreHeader);
  memcpy(b, makers, maker_bytes);
  memcpy(b + maker_bytes, pairs, (size_t)pair_n * sizeof(FdbPair));

  StoreHeader h;
  memset(&h, 0, sizeof(h));
  h.magic   = STORE_MAGIC;
  h.version = STORE_VERSION;
  h.maker_n = (uint16_t)maker_n;
  h.pair_n  = (uint16_t)pair_n;
  h.crc     = esp_rom_crc32_le(0, b, body);
  h.stamp   = now;
  snprintf(h.base_url, sizeof(h.base_url), "%s", base_url);
  memcpy(buf, &h, sizeof(h));

  s_hand_buf   = buf;
  s_hand_len   = total;
  s_hand_since = millis();
  // The copy before the flag: the loop reads the buffer the moment it sees it.
  __sync_synchronize();
  s_hand = HAND_BUILT;
}

// Body first, header last: a store cut short leaves no magic and reads as
// none rather than as a broken one.
static bool writeStore(const esp_partition_t* part, const StoreHeader& h, const uint8_t* body,
                       size_t body_len) {
  const size_t total = sizeof(h) + body_len;
  const size_t erase = (total + STORE_SECTOR - 1) / STORE_SECTOR * STORE_SECTOR;
  return esp_partition_erase_range(part, STORE_OFFSET, erase) == ESP_OK &&
         esp_partition_write(part, STORE_OFFSET + sizeof(h), body, body_len) == ESP_OK &&
         esp_partition_write(part, STORE_OFFSET, &h, sizeof(h)) == ESP_OK;
}

static void fdbStoreTask(void* arg) {
  (void)arg;
  const uint32_t heap_start = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  StoreHeader h;
  memcpy(&h, s_hand_buf, sizeof(h));
  const size_t total = s_hand_len;
  const bool ok = writeStore(s_store_part, h, s_hand_buf + sizeof(h), total - sizeof(h));
  logSDf("Filament DB: index kept in flash: %s, %u bytes", ok ? "ok" : "write failed", (unsigned)total);
  logSDf("Filament DB: store task done, internal heap %u at start, stack left %u of %u",
         (unsigned)heap_start, (unsigned)uxTaskGetStackHighWaterMark(NULL),
         (unsigned)FDB_STORE_STACK_BYTES);
  // The verdict after everything else: the loop frees the copy the moment
  // it sees it.
  __sync_synchronize();
  s_hand = ok ? HAND_STORED : HAND_FAILED;
  workerTaskPark(&s_store_task);
}

static void dropHanded() {
  heap_caps_free(s_hand_buf);
  s_hand_buf = nullptr;
  s_hand_len = 0;
  __sync_synchronize();
  s_hand = HAND_IDLE;
}

void fdbStoreTick() {
  const int hand = s_hand;
  if (hand == HAND_IDLE || hand == HAND_STORING) return;
  if (hand == HAND_STORED || hand == HAND_FAILED) {
    // Parked a moment after the verdict; until then the copy stays.
    if (!workerTaskReap(&s_store_task)) return;
    dropHanded();
    return;
  }
  // HAND_BUILT: a copy waits for the store.
  if (millis() - s_hand_since > FDB_STORE_START_WAIT_MS) {
    logSDf("Filament DB: index not kept, the heap stayed below %u for %lu s",
           (unsigned)FDB_STORE_MIN_HEAP, (unsigned long)(FDB_STORE_START_WAIT_MS / 1000UL));
    dropHanded();
    logGateReset(&s_store_gate);
    return;
  }
  // workerFreeHeap() counts the database task's stack in PSRAM as taken
  // while that task is there: after the hand-over it still asks the server
  // whose spools the inventory has, a request in flight. So the store waits
  // for the reserve beside that request, as every other worker does.
  // NOT-TO-DO: adding that stack back the way the Bambu catalog's store does
  //   (workerFreeHeap() + webJobOutsideStackBytes()) - that job only waits
  //   for its store, this one is still talking to the server.
  const uint32_t free_heap = workerFreeHeap();
  const WorkerStackPlan plan = {false, free_heap >= FDB_STORE_MIN_HEAP};
  if (!plan.inside) {
    if (logGateDue(&s_store_gate, millis(), BACKEND_JOB_REFUSAL_LOG_MS)) {
      logSDf("Filament DB: store postponed, heap %u < %u (%u more refusals since the last line)",
             (unsigned)free_heap, (unsigned)FDB_STORE_MIN_HEAP,
             (unsigned)logGateTakeSuppressed(&s_store_gate));
    }
    return;
  }
  // Found here on the loop: the store task with its small stack only writes.
  s_store_part = dataPartition();
  if (!s_store_part) {
    logSD("Filament DB: no room in the data partition to keep the index");
    dropHanded();
    return;
  }
  s_hand = HAND_STORING;
  if (!workerTaskStart(&s_store_task, plan,
                       WorkerTaskSpec{fdbStoreTask, "fdbstore", FDB_STORE_STACK_BYTES,
                                      FDB_STORE_PRIORITY, FDB_STORE_CORE})) {
    s_hand = HAND_BUILT;   // tried again on the next pass, or dropped after the wait
    if (logGateDue(&s_store_gate, millis(), BACKEND_JOB_REFUSAL_LOG_MS)) {
      logSDf("Filament DB: store task creation failed (%u more since the last line)",
             (unsigned)logGateTakeSuppressed(&s_store_gate));
    }
    return;
  }
  logGateReset(&s_store_gate);
}
