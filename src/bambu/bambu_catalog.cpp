#include "bambu_catalog.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_partition.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "hardware/flash_log.h"
#include "hardware/sd_logger.h"
#include "services/backend_http.h"
#include "services/github_release.h"
#include "services/worker_task.h"
#include "web/web_jobs.h"

// Last: T() is a macro, and ArduinoJson uses T as a template parameter.
#include "lang.h"

// The table as BambuStudio ships it, from its default branch: Bambu adds
// colours there when they go on sale.
#define BAMBU_CATALOG_URL \
  "https://raw.githubusercontent.com/bambulab/BambuStudio/master/" \
  "resources/profiles/BBL/filament/filaments_color_codes.json"
#define BAMBU_CATALOG_TIMEOUT_MS  15000
// 230 kB today; room for Bambu to keep adding colours for a while.
#define BAMBU_CATALOG_DOWNLOAD_MAX  (1024UL * 1024UL)
// No byte for this long on an open socket: stuck, not slow.
#define BAMBU_CATALOG_STALL_MS    20000UL
#define BAMBU_CATALOG_POLL_MS     10

// The write to flash runs on a task of its own (V59). Its stack lies in
// internal RAM, which a task that writes the flash needs, and it opens no
// connection: 4 kB hold esp_partition_* and one log line (logSDf's 256 B);
// the high-water mark is in the log.
#define CAT_STORE_STACK_BYTES    4096
#define CAT_STORE_PRIORITY       1
#define CAT_STORE_CORE           0
// What a background request leaves of the internal heap at its lowest
// (Spec 8 T: the 50 000 floor less the at most 24 000 of the request). The
// store starts with that and its own stack free, so the rest of the firmware
// keeps as much while the table is written as while it is downloaded.
#define CAT_STORE_RESERVE_BYTES  26000UL
#define CAT_STORE_MIN_HEAP       (CAT_STORE_RESERVE_BYTES + CAT_STORE_STACK_BYTES)
// How long the download job waits for the loop to start the store before it
// gives the converted copy up and reports a failure (the hour's retry of
// bambu_catalog_sync.cpp then asks again).
#define CAT_STORE_START_WAIT_MS  60000UL
#define CAT_STORE_POLL_MS        50

#define CAT_OFFSET        FLASH_LOG_BYTES
#define CAT_SECTOR        4096UL
#define CAT_MAGIC         0x54414342UL   // "BCAT"
// 2: the ETag the copy was downloaded under. 3: how the colours of a record
// lie on the spool. 4: the same, read for real - version 3 had dropped the
// colour type in the download filter, so every record was single. A copy of
// an older version counts as none, so the scale downloads the table again on
// its own (bambu_catalog_sync.cpp).
#define CAT_VERSION       4
#define CAT_COLOURS       BAMBU_CATALOG_COLOURS   // gradients and multi colour spools carry up to four
#define CAT_MAX_ENTRIES   1200
// A stamp before this is a clock that was never set, not a date.
#define CAT_STAMP_MIN     1700000000UL

// ---- the stored form ------------------------------------------
// A header, fixed records, then every string once in a pool the records
// point into. Written header last, so a store cut short leaves no magic and
// reads as no catalog rather than as a broken one.
struct CatHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  uint32_t pool_bytes;
  uint32_t crc;         // over records and pool
  uint32_t stamp;
  // What GitHub called this version of the file. Sent back as If-None-Match,
  // it turns the daily check into a 304 without a body while nothing changed.
  char     etag[96];
  uint8_t  reserved[12];
};
static_assert(sizeof(CatHeader) == 128, "the header is 128 bytes on the device and the host");

struct CatRec {
  char     id[6];       // "GFA10"
  char     code[4];     // "B0", as the table spells it
  uint8_t  ncol;
  uint8_t  kind;        // BambuColorKind
  char     article[8];  // "12601"
  uint32_t rgba[CAT_COLOURS];   // 0xRRGGBBAA, the first colour first
  uint16_t product, en, de, fr;         // offsets into the pool
};
static_assert(sizeof(CatRec) == 44, "a record is 44 bytes on the device and the host");

// The PSRAM copy the lookups read. Loaded on the first lookup and again after
// a store; only the loop task touches it.
static uint8_t*          s_blob    = nullptr;
static const CatHeader*  s_hdr     = nullptr;
static bool              s_tried   = false;
// Set by the store on the worker task, read by the loop task.
static volatile bool     s_storing = false;
static volatile bool     s_dirty   = false;
// When the last download or check reached GitHub, for the loop to keep.
static volatile uint32_t s_checked = 0;

// The hand-over between the download job and the store task, see
// bambuCatalogStoreTick(). The job converts the table into a copy in PSRAM,
// sets CH_BUILT and waits; the loop moves it to CH_STORING and starts the
// store task; that one writes the copy and sets CH_STORED or CH_FAILED; the
// job frees the copy and sets CH_IDLE. A copy the store never started on is
// taken back by the job, CH_BUILT to CH_IDLE, with the same compare-and-swap
// the loop uses, so the two never both own it.
enum CatHand : int { CH_IDLE = 0, CH_BUILT, CH_STORING, CH_STORED, CH_FAILED };
static volatile int      s_hand     = CH_IDLE;
static uint8_t* volatile s_hand_buf = nullptr;
static volatile size_t   s_hand_len = 0;
static char              s_hand_err[64] = "";
static WorkerTask        s_store_task = {};
static LogGate           s_store_gate = {};   // the "store postponed" line
// The ETag the loop read for the next download (bambuCatalogPrepareDownload).
static char              s_job_etag[96] = "";

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

static uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t c = 0xFFFFFFFFUL;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320UL & (0UL - (c & 1)));
  }
  return ~c;
}

static const esp_partition_t* dataPartition() {
  const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                      ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
  if (!p || p->size < CAT_OFFSET + BAMBU_CATALOG_BYTES) return nullptr;
  return p;
}

// ---- loading, loop task ---------------------------------------

static void unload() {
  if (s_blob) heap_caps_free(s_blob);
  s_blob = nullptr;
  s_hdr  = nullptr;
}

static void ensureLoaded() {
  if (s_storing) return;
  if (s_dirty) { s_dirty = false; unload(); s_tried = false; }
  if (s_tried) return;
  s_tried = true;

  const esp_partition_t* part = dataPartition();
  if (!part) return;
  CatHeader h;
  if (esp_partition_read(part, CAT_OFFSET, &h, sizeof(h)) != ESP_OK) return;
  if (h.magic != CAT_MAGIC || h.version != CAT_VERSION) return;
  const size_t body = (size_t)h.count * sizeof(CatRec) + h.pool_bytes;
  if (h.count == 0 || h.count > CAT_MAX_ENTRIES ||
      sizeof(h) + body > BAMBU_CATALOG_BYTES) {
    logSDf("Bambu catalog: header out of range (%u colours, %lu pool bytes)",
           (unsigned)h.count, (unsigned long)h.pool_bytes);
    return;
  }
  uint8_t* blob = (uint8_t*)heap_caps_malloc(sizeof(h) + body, MALLOC_CAP_SPIRAM);
  if (!blob) blob = (uint8_t*)malloc(sizeof(h) + body);
  if (!blob) return;
  if (esp_partition_read(part, CAT_OFFSET, blob, sizeof(h) + body) != ESP_OK ||
      crc32(blob + sizeof(h), body) != h.crc) {
    logSD("Bambu catalog: stored copy fails its checksum, ignored");
    heap_caps_free(blob);
    return;
  }
  s_blob = blob;
  s_hdr  = (const CatHeader*)blob;
  logSDf("Bambu catalog: %u colours loaded", (unsigned)h.count);
}

static const CatRec* recs() { return (const CatRec*)(s_blob + sizeof(CatHeader)); }

static const char* poolStr(uint16_t off) {
  const char* pool = (const char*)(s_blob + sizeof(CatHeader) + s_hdr->count * sizeof(CatRec));
  return off < s_hdr->pool_bytes ? pool + off : "";
}

// The table names how the colours lie on the spool in Chinese: 渐变色 is a
// gradient along the filament, 多拼色 colours side by side across it (dual and
// tri colour silk), 单色 a single colour. Compared as UTF-8 bytes.
#define CAT_TYPE_GRADIENT  "\xE6\xB8\x90\xE5\x8F\x98\xE8\x89\xB2"
#define CAT_TYPE_MULTI     "\xE5\xA4\x9A\xE6\x8B\xBC\xE8\x89\xB2"

static uint8_t colourKind(const char* type) {
  if (strcmp(type, CAT_TYPE_GRADIENT) == 0) return BCK_GRADIENT;
  if (strcmp(type, CAT_TYPE_MULTI) == 0)    return BCK_DUAL;
  return BCK_SINGLE;
}

// "B0", "B00", "G6", "G06": the tag and the table do not agree on the zeros
// (PLA Basic Bambu Green is A00-G06 on the spool, G6 in the table), so a
// colour code is compared as its letter and its number.
static bool codeKey(const char* s, char* letter, int* num) {
  if (!s || !s[0] || s[0] < 'A' || s[0] > 'Z') return false;
  *letter = s[0];
  char* end = nullptr;
  const long n = strtol(s + 1, &end, 10);
  if (end == s + 1 || *end) return false;
  *num = (int)n;
  return true;
}

static bool sameColour(uint32_t rgba, const SpoolColor& c) {
  return c.valid && (rgba >> 8) == c.rgb && (uint8_t)(rgba & 0xFF) == c.alpha;
}

// What a lookup hands back of one record, the colour's name in the UI
// language where the table has it.
static void fillHit(const CatRec& r, BambuCatalogHit* out) {
  snprintf(out->article, sizeof(out->article), "%s", r.article);
  snprintf(out->product, sizeof(out->product), "%s", poolStr(r.product));
  const uint16_t local = g_lang == LANG_DE ? r.de : g_lang == LANG_FR ? r.fr : r.en;
  const char* name = poolStr(local);
  snprintf(out->color_name, sizeof(out->color_name), "%s", name[0] ? name : poolStr(r.en));
  snprintf(out->color_name_en, sizeof(out->color_name_en), "%s", poolStr(r.en));
  out->kind = r.kind;
  out->ncol = r.ncol < CAT_COLOURS ? r.ncol : CAT_COLOURS;
  for (uint8_t i = 0; i < out->ncol; i++) out->rgba[i] = r.rgba[i];
}

bool bambuCatalogFind(const char* material_id, const char* variant_id,
                      const SpoolColor& color, BambuCatalogHit* out) {
  ensureLoaded();
  if (!s_hdr || !material_id || !material_id[0] || !out) return false;

  char want_l = 0;
  int  want_n = -1;
  const char* dash = variant_id ? strchr(variant_id, '-') : nullptr;
  const bool have_code = dash && codeKey(dash + 1, &want_l, &want_n);

  // The code decides where it matches and the colour agrees. Where the two
  // disagree the colour wins if another entry of the product has exactly
  // it: a batch whose code the table spells differently still has its own
  // colour. The code alone is the last resort.
  const CatRec* by_code = nullptr;
  const CatRec* by_colour = nullptr;
  for (int i = 0; i < s_hdr->count; i++) {
    const CatRec& r = recs()[i];
    if (strncmp(r.id, material_id, sizeof(r.id)) != 0) continue;
    char l; int n;
    if (have_code && codeKey(r.code, &l, &n) && l == want_l && n == want_n) {
      if (sameColour(r.rgba[0], color)) { by_code = &r; by_colour = &r; break; }
      if (!by_code) by_code = &r;
    }
    if (!by_colour && sameColour(r.rgba[0], color)) by_colour = &r;
  }
  const CatRec* hit = by_colour ? by_colour : by_code;
  if (!hit) return false;
  fillHit(*hit, out);
  return true;
}

bool bambuCatalogFindByLook(const char* product, uint32_t rgb, const char* name_en,
                            BambuCatalogHit* out) {
  ensureLoaded();
  if (!s_hdr || !product || !product[0] || !out) return false;
  const CatRec* by_name = nullptr;
  for (int i = 0; i < s_hdr->count; i++) {
    const CatRec& r = recs()[i];
    if (strcasecmp(poolStr(r.product), product) != 0) continue;
    if ((r.rgba[0] >> 8) == (rgb & 0xFFFFFFUL)) { fillHit(r, out); return true; }
    if (!by_name && name_en && name_en[0] && strcasecmp(poolStr(r.en), name_en) == 0) by_name = &r;
  }
  if (!by_name) return false;
  fillHit(*by_name, out);
  return true;
}

int bambuCatalogCount() {
  ensureLoaded();
  return s_hdr ? s_hdr->count : 0;
}

uint32_t bambuCatalogStamp() {
  ensureLoaded();
  return s_hdr ? s_hdr->stamp : 0;
}

// ---- converting, on the download job; storing, on the store task -

// Appends s to the pool, or finds it there already: a product name stands
// behind dozens of colours and is stored once.
static bool poolAdd(uint8_t* pool, size_t cap, size_t* used, const char* s, uint16_t* off) {
  if (!s) s = "";
  const size_t len = strlen(s) + 1;
  for (size_t i = 0; i + len <= *used; ) {
    const size_t l = strlen((const char*)pool + i) + 1;
    if (l == len && memcmp(pool + i, s, len) == 0) { *off = (uint16_t)i; return true; }
    i += l;
  }
  if (*used + len > cap || *used + len > 0xFFFF) return false;
  memcpy(pool + *used, s, len);
  *off = (uint16_t)*used;
  *used += len;
  return true;
}

static uint32_t parseRgba(const char* hex) {
  if (!hex || hex[0] != '#') return 0;
  const size_t n = strlen(hex + 1);
  uint32_t v = strtoul(hex + 1, nullptr, 16);
  if (n == 6) v = (v << 8) | 0xFF;
  else if (n != 8) return 0;
  return v;
}

// A copy catalogBuild() made: what the flash will hold, in PSRAM.
struct CatCopy {
  uint8_t* buf;       // header, records, pool; heap_caps_free() when done
  size_t   total;     // the bytes of it to write
  int      count;     // the colours in it
  char     err[64];   // why there is none
};

// The copy as the flash holds it, header, records and pool, in PSRAM, into
// *copy. Touches no flash, so it may run on the download job (V59).
static bool catalogBuild(JsonArrayConst entries, uint32_t stamp, const char* etag,
                         CatCopy* copy) {
  *copy = {};
  char* err = copy->err;
  const size_t err_len = sizeof(copy->err);
  const size_t n_in = entries.size();
  if (n_in == 0) { snprintf(err, err_len, "the table is empty"); return false; }

  uint8_t* buf = (uint8_t*)heap_caps_malloc(BAMBU_CATALOG_BYTES, MALLOC_CAP_SPIRAM);
  if (!buf) { snprintf(err, err_len, "no memory"); return false; }
  memset(buf, 0, BAMBU_CATALOG_BYTES);

  // Records first, then the pool where the room ends: its size is only known
  // once every string is in.
  CatRec* out = (CatRec*)(buf + sizeof(CatHeader));
  const size_t max_recs = (BAMBU_CATALOG_BYTES - sizeof(CatHeader)) / (sizeof(CatRec) + 16);
  uint8_t* pool = (uint8_t*)heap_caps_malloc(BAMBU_CATALOG_BYTES, MALLOC_CAP_SPIRAM);
  if (!pool) { heap_caps_free(buf); snprintf(err, err_len, "no memory"); return false; }
  size_t used = 0;
  uint16_t empty_off = 0;
  poolAdd(pool, BAMBU_CATALOG_BYTES, &used, "", &empty_off);

  size_t n = 0;
  bool full = false;
  for (JsonObjectConst e : entries) {
    const char* id   = e["fila_id"] | "";
    const char* code = e["color_code"] | "";
    if (strlen(id) != 5 || !code[0] || strlen(code) > 3) continue;   // not a tag's pair
    if (n >= max_recs) { full = true; break; }
    CatRec& r = out[n];
    memset(&r, 0, sizeof(r));
    memcpy(r.id, id, 5);
    memcpy(r.code, code, strlen(code));
    snprintf(r.article, sizeof(r.article), "%s", e["fila_color_code"] | "");
    for (JsonVariantConst c : e["fila_color"].as<JsonArrayConst>()) {
      if (r.ncol >= CAT_COLOURS) break;
      r.rgba[r.ncol++] = parseRgba(c | "");
    }
    r.kind = colourKind(e["fila_color_type"] | "");
    JsonObjectConst names = e["fila_color_name"];
    if (!poolAdd(pool, BAMBU_CATALOG_BYTES, &used, e["fila_type"] | "", &r.product) ||
        !poolAdd(pool, BAMBU_CATALOG_BYTES, &used, names["en"] | "", &r.en) ||
        !poolAdd(pool, BAMBU_CATALOG_BYTES, &used, names["de"] | "", &r.de) ||
        !poolAdd(pool, BAMBU_CATALOG_BYTES, &used, names["fr"] | "", &r.fr)) {
      full = true;
      break;
    }
    n++;
  }
  const size_t body = n * sizeof(CatRec) + used;
  if (n == 0 || full || sizeof(CatHeader) + body > BAMBU_CATALOG_BYTES) {
    heap_caps_free(pool);
    heap_caps_free(buf);
    snprintf(err, err_len, n ? "the table no longer fits in %lu kB" : "no usable entries",
             (unsigned long)(BAMBU_CATALOG_BYTES / 1024));
    return false;
  }
  memcpy(buf + sizeof(CatHeader) + n * sizeof(CatRec), pool, used);
  heap_caps_free(pool);

  CatHeader* h = (CatHeader*)buf;
  h->magic      = CAT_MAGIC;
  h->version    = CAT_VERSION;
  h->count      = (uint16_t)n;
  h->pool_bytes = (uint32_t)used;
  h->crc        = crc32(buf + sizeof(CatHeader), body);
  h->stamp      = stamp >= CAT_STAMP_MIN ? stamp : 0;
  snprintf(h->etag, sizeof(h->etag), "%s", etag ? etag : "");

  copy->count = (int)n;
  copy->buf   = buf;
  copy->total = sizeof(CatHeader) + body;
  return true;
}

// Writes a copy catalogBuild() made, total bytes long, and reads it back. On
// a task whose stack lies in internal RAM: the store task, or the caller of
// bambuCatalogStore(). buf is read back into; the caller frees it.
static bool catalogWrite(uint8_t* buf, size_t total, char* err, size_t err_len) {
  const esp_partition_t* part = dataPartition();
  if (!part) { snprintf(err, err_len, "no data partition"); return false; }
  const CatHeader* h = (const CatHeader*)buf;
  const uint32_t crc = h->crc;
  const unsigned n   = h->count;
  const size_t body  = total - sizeof(CatHeader);

  // Only the sectors the new copy needs. Erasing the whole 64 kB would stall
  // both cores for close to a second, for nothing.
  const size_t erase = (total + CAT_SECTOR - 1) / CAT_SECTOR * CAT_SECTOR;
  s_storing = true;
  bool ok = esp_partition_erase_range(part, CAT_OFFSET, erase) == ESP_OK &&
            esp_partition_write(part, CAT_OFFSET + sizeof(CatHeader),
                                buf + sizeof(CatHeader), body) == ESP_OK &&
            esp_partition_write(part, CAT_OFFSET, buf, sizeof(CatHeader)) == ESP_OK;
  if (ok) {
    // Read back and checked, as the loader will: a store that cannot be
    // loaded is reported now, not found out at the next scan.
    ok = esp_partition_read(part, CAT_OFFSET, buf, total) == ESP_OK &&
         crc32(buf + sizeof(CatHeader), body) == crc;
  }
  s_dirty   = true;
  s_storing = false;
  if (!ok) { snprintf(err, err_len, "writing to flash failed"); return false; }
  logSDf("Bambu catalog: %u colours stored, %lu bytes", n, (unsigned long)total);
  return true;
}

bool bambuCatalogStore(JsonArrayConst entries, uint32_t stamp, const char* etag,
                       char* err, size_t err_len, int* count) {
  if (count) *count = 0;
  CatCopy copy;
  if (!catalogBuild(entries, stamp, etag, &copy)) {
    snprintf(err, err_len, "%s", copy.err);
    return false;
  }
  const bool ok = catalogWrite(copy.buf, copy.total, err, err_len);
  heap_caps_free(copy.buf);
  if (ok && count) *count = copy.count;
  return ok;
}

// The ETag of the stored copy, straight from flash. Loop task: the download
// job must not touch the flash, nor the loop's PSRAM copy.
static bool storedEtag(char* out, size_t n) {
  out[0] = '\0';
  const esp_partition_t* part = dataPartition();
  CatHeader h;
  if (!part || esp_partition_read(part, CAT_OFFSET, &h, sizeof(h)) != ESP_OK) return false;
  if (h.magic != CAT_MAGIC || h.version != CAT_VERSION) return false;
  h.etag[sizeof(h.etag) - 1] = '\0';
  snprintf(out, n, "%s", h.etag);
  return out[0] != '\0';
}

void bambuCatalogPrepareDownload(bool conditional) {
  s_job_etag[0] = '\0';
  if (conditional) storedEtag(s_job_etag, sizeof(s_job_etag));
}

static void noteChecked() {
  const time_t now = time(nullptr);
  s_checked = (uint32_t)now >= CAT_STAMP_MIN ? (uint32_t)now : 1;
}

uint32_t bambuCatalogTakeChecked() {
  const uint32_t c = s_checked;
  s_checked = 0;
  return c;
}

// ---- the store task and the hand-over --------------------------
//
// Wurzel: the daily check needed 50 000 B of free internal heap, because its
//   job read the stored ETag and wrote a changed table on its own task, and a
//   stack in PSRAM must not be in use during a flash access (ESP-IDF 5.5,
//   components/spi_flash/cache_utils.c line 141, the assert in
//   spi_flash_disable_interrupts_caches_and_other_cpu()). The 7B has 49.4 to
//   50.2 kB free in operation; on 31.10./01.11.2026 it was refused eleven
//   times in a row (Punkteliste n50).
// Fix (V59): the loop reads the ETag before the job starts; the job only
//   downloads and converts, so its stack may lie in PSRAM and its floor drops
//   by that stack like the GitHub check's; the write runs on a store task
//   with a 4 kB stack in internal RAM, which the loop starts with the reserve
//   of a background request free. The job waits for the store, so its result
//   still says whether the table was stored.
// Verworfen: the floor of this one job lowered by hand - the low point during
//   the request would fall under the 26 kB of Spec 8 T, and the next few
//   hundred bytes of heap would bring the refusals back. The write on the
//   loop task itself - the loop would wait out the erase of the sectors, up
//   to a second without a picture, where the store task leaves it a slice
//   between two sectors as the job did.
// Test: test/test_bambu_catalog_store.py, test/test_worker_stack.py (3).

static void catalogStoreTask(void* arg) {
  (void)arg;
  char err[64] = "";
  const bool ok = catalogWrite(s_hand_buf, s_hand_len, err, sizeof(err));
  snprintf(s_hand_err, sizeof(s_hand_err), "%s", err);
  logSDf("Bambu catalog: store task %s, stack left %u of %u", ok ? "done" : "failed",
         (unsigned)uxTaskGetStackHighWaterMark(NULL), (unsigned)CAT_STORE_STACK_BYTES);
  // The verdict after everything else: the job frees the copy the moment it
  // sees it.
  __sync_synchronize();
  s_hand = ok ? CH_STORED : CH_FAILED;
  workerTaskPark(&s_store_task);
}

void bambuCatalogStoreTick() {
  if (s_hand != CH_BUILT || s_store_task.handle) return;
  // workerFreeHeap() counts the download job's stack in PSRAM as taken: it
  // stands for the connection of a request in flight. This job's request is
  // over when it hands the copy over, it only waits, so its stack is not
  // counted against the store. Counted, the store wanted 46 kB of real heap
  // and was postponed at "30084 < 30096" on the 7B (acceptance of beta.19).
  const uint32_t free_heap = workerFreeHeap() + webJobOutsideStackBytes();
  const WorkerStackPlan plan = {false, free_heap >= CAT_STORE_MIN_HEAP};
  if (!plan.inside) {
    if (logGateDue(&s_store_gate, millis(), BACKEND_JOB_REFUSAL_LOG_MS)) {
      logSDf("Bambu catalog: store postponed, heap %u < %u (%u more refusals since the last line)",
             (unsigned)free_heap, (unsigned)CAT_STORE_MIN_HEAP,
             (unsigned)logGateTakeSuppressed(&s_store_gate));
    }
    return;
  }
  // The job may have given the copy up a moment ago; then it is not ours.
  if (!__sync_bool_compare_and_swap(&s_hand, (int)CH_BUILT, (int)CH_STORING)) return;
  if (!workerTaskStart(&s_store_task, plan,
                       WorkerTaskSpec{catalogStoreTask, "catstore", CAT_STORE_STACK_BYTES,
                                      CAT_STORE_PRIORITY, CAT_STORE_CORE})) {
    s_hand = CH_BUILT;   // tried again on the next pass, or given up by the job
    if (logGateDue(&s_store_gate, millis(), BACKEND_JOB_REFUSAL_LOG_MS)) {
      logSDf("Bambu catalog: store task creation failed (%u more since the last line)",
             (unsigned)logGateTakeSuppressed(&s_store_gate));
    }
    return;
  }
  logGateReset(&s_store_gate);
}

// The download job's side: hands the copy over and waits for the verdict.
// Frees the copy in every case.
static bool catalogAwaitStore(uint8_t* buf, size_t total, char* err, size_t err_len) {
  s_hand_buf    = buf;
  s_hand_len    = total;
  s_hand_err[0] = '\0';
  __sync_synchronize();
  s_hand = CH_BUILT;
  const unsigned long since = millis();
  for (;;) {
    const int h = s_hand;
    if (h == CH_STORED || h == CH_FAILED) break;
    if (h == CH_BUILT && millis() - since > CAT_STORE_START_WAIT_MS &&
        __sync_bool_compare_and_swap(&s_hand, (int)CH_BUILT, (int)CH_IDLE)) {
      s_hand_buf = nullptr;
      heap_caps_free(buf);
      snprintf(err, err_len, "not stored, too little heap for the store");
      return false;
    }
    delay(CAT_STORE_POLL_MS);
  }
  const bool ok = (s_hand == CH_STORED);
  if (!ok) snprintf(err, err_len, "%s", s_hand_err);
  s_hand_buf = nullptr;
  s_hand_len = 0;
  heap_caps_free(buf);
  __sync_synchronize();
  s_hand = CH_IDLE;
  return ok;
}

BambuCatalogOutcome bambuCatalogDownload(char* err, size_t err_len, int* count) {
  if (count) *count = 0;
  WiFiClientSecure client;
  githubTrust(client);
  HTTPClient http;
  http.setTimeout(BAMBU_CATALOG_TIMEOUT_MS);
  if (!http.begin(client, BAMBU_CATALOG_URL)) {
    snprintf(err, err_len, "connection failed");
    return BCO_FAILED;
  }
  const char* keep[] = { "ETag", "Transfer-Encoding" };
  http.collectHeaders(keep, 2);
  // Read by the loop before this job started (bambuCatalogPrepareDownload).
  if (s_job_etag[0]) http.addHeader("If-None-Match", s_job_etag);
  const int code = http.GET();
  if (code == 304) {
    http.end();
    noteChecked();
    logSD("Bambu catalog: unchanged at BambuStudio (304)");
    return BCO_UNCHANGED;
  }
  if (code != 200) {
    snprintf(err, err_len, "HTTP %d", code);
    http.end();
    return BCO_FAILED;
  }
  char etag[96];
  snprintf(etag, sizeof(etag), "%s", http.header("ETag").c_str());

  // The whole file first, then the parse. Parsing straight off the TLS
  // stream gave up the moment the data paused ("IncompleteInput" after 2 s
  // on the device, 28.09.2026): the parser takes a quiet socket for the end.
  const int size = http.getSize();
  if (size > (int)BAMBU_CATALOG_DOWNLOAD_MAX) {
    snprintf(err, err_len, "the table is %d bytes, more than expected", size);
    http.end();
    return BCO_FAILED;
  }
  const size_t cap = size > 0 ? (size_t)size : BAMBU_CATALOG_DOWNLOAD_MAX;
  char* raw = (char*)heap_caps_malloc(cap + 1, MALLOC_CAP_SPIRAM);
  if (!raw) { snprintf(err, err_len, "no memory"); http.end(); return BCO_FAILED; }
  Stream* in = http.getStreamPtr();
  // GitHub sends the file with a Content-Length. Sent chunked instead, the
  // raw socket carries a size line before every piece, and the parser would
  // take those for JSON; ChunkedStream takes them out. Its timeout is 0
  // because it waits on the socket itself, and the end is its last chunk,
  // not a closed socket.
  const bool chunked = http.header("Transfer-Encoding").equalsIgnoreCase("chunked");
  ChunkedStream dechunk;
  if (chunked) {
    dechunk.reset(in);
    dechunk.setTimeout(0);
    in = &dechunk;
    logSD("Bambu catalog: the answer is chunked, size lines taken out");
  }
  size_t got = 0;
  unsigned long last = millis();
  while (got < cap) {
    const int avail = in->available();
    if (avail > 0) {
      const size_t want = (size_t)avail < cap - got ? (size_t)avail : cap - got;
      const int n = in->readBytes(raw + got, want);
      if (n > 0) { got += (size_t)n; last = millis(); }
      continue;
    }
    if (chunked && dechunk.done()) break;
    if (!http.connected()) break;
    if (millis() - last > BAMBU_CATALOG_STALL_MS) break;
    delay(BAMBU_CATALOG_POLL_MS);
  }
  http.end();
  if (size > 0 && got != (size_t)size) {
    heap_caps_free(raw);
    snprintf(err, err_len, "download stopped at %u of %d bytes", (unsigned)got, size);
    return BCO_FAILED;
  }
  raw[got] = '\0';

  // Eight fields per colour of a 230 kB file, the colour type among them:
  // catalogBuild() reads it for the kind. The rest - nine more
  // languages - is dropped while it is parsed.
  JsonDocument filter;
  JsonObject f = filter["data"].to<JsonArray>().add<JsonObject>();
  f["fila_id"] = true;
  f["color_code"] = true;
  f["fila_color_code"] = true;
  f["fila_type"] = true;
  f["fila_color"] = true;
  f["fila_color_type"] = true;
  f["fila_color_name"]["en"] = true;
  f["fila_color_name"]["de"] = true;
  f["fila_color_name"]["fr"] = true;

  SpiRamAllocator psram;
  JsonDocument doc(&psram);
  const DeserializationError e =
      deserializeJson(doc, (const char*)raw, got, DeserializationOption::Filter(filter));
  heap_caps_free(raw);
  if (e) {
    snprintf(err, err_len, "the table did not parse: %s", e.c_str());
    return BCO_FAILED;
  }
  CatCopy copy;
  const bool built = catalogBuild(doc["data"].as<JsonArrayConst>(), (uint32_t)time(nullptr), etag,
                                  &copy);
  // The parsed table is not needed while the store runs.
  doc.clear();
  if (!built) {
    snprintf(err, err_len, "%s", copy.err);
    return BCO_FAILED;
  }
  if (!catalogAwaitStore(copy.buf, copy.total, err, err_len)) return BCO_FAILED;
  if (count) *count = copy.count;
  noteChecked();
  return BCO_UPDATED;
}
