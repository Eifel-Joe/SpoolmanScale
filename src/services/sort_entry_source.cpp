#include "services/sort_entry_source.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <initializer_list>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "hardware/sd_logger.h"
#include "services/backend.h"
#include "services/backend_api.h"
#include "services/bambuddy_api.h"
#include "services/filament_db.h"
#include "services/http_progress.h"
#include "services/spoolman_api.h"
#include "services/text_util.h"

// Each request to the inventory holds the loop for at most this long (plan:
// 8 s); the screen shows its waiting card before it asks.
#define SORT_HTTP_TIMEOUT_MS  8000
// A database load that filament_db refused while it was free (heap below the
// floor, no PSRAM for the list) is asked for again after this long, as the
// picker does (DBP_RETRY_MS), and left out after so many refusals: the entry
// goes on with what it has rather than wait on a heap that stays low.
#define SORT_RETRY_MS         2000
#define SORT_REFUSALS_MAX     5
// The chosen material's load and those of the maker's three most frequent
// other materials (plan, Festlegung 3).
#define SORT_OTHER_MATERIALS  3
#define SORT_JOBS_MAX         (1 + SORT_OTHER_MATERIALS)

// Spoolman. In quotes a name filter matches exactly. The spools are asked by
// filament.vendor.name: vendor.name on /spool is passed over and answers
// every spool (checked on Spoolman 0.27, 26.10.2026). Archived spools too:
// a filament whose spools are used up keeps the colour name they carried.
#define SM_PATH_VENDOR        "/api/v1/vendor"
#define SM_PATH_FILAMENT      "/api/v1/filament?vendor.name="
#define SM_PATH_SPOOL         "/api/v1/spool?allow_archived=true&filament.vendor.name="
// Where Bambuddy keeps a spool's colour name, as JSON text ("\"Light Green\"").
#define SM_EXTRA_COLOUR       "bambu_color_name"

namespace {

// ArduinoJson has to be told to use PSRAM, and every translation unit that
// needs it defines its own allocator (see bambuddy_filament_db.cpp).
struct SpiRamAllocator : ArduinoJson::Allocator {
  void* allocate(size_t size) override {
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!ptr) ptr = malloc(size);
    return ptr;
  }
  void deallocate(void* pointer) override { heap_caps_free(pointer); }
  void* reallocate(void* ptr, size_t new_size) override {
    void* p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM);
    if (!p) p = realloc(ptr, new_size);
    return p;
  }
};

SpiRamAllocator s_psram;

struct ListRead {
  JsonDocument* doc;
  JsonDocument* filter;
};

// A database load of one maker and material, in the order they are asked.
struct SortJob {
  char    maker[32];      // as the index spells it, which the loader compares
  char    material[17];
  uint8_t from;           // SF_DB for the chosen material, SF_DB_OTHER for the rest
  uint8_t refusals;       // starts filament_db refused
  bool    restarted;      // vanished once and asked again
};

// Where the inventory is read.
enum SortWay : uint8_t { WAY_NONE = 0, WAY_SPOOLMAN, WAY_BAMBUDDY };

}  // namespace

// What s_live holds besides the index of the load that is out.
static const int JOB_NONE  = -1;   // no load of ours is out
static const int JOB_STALE = -2;   // one for a material no longer chosen: dropped when it arrives

static SortData* s_data       = nullptr;
static SortJob   s_jobs[SORT_JOBS_MAX];
static int       s_job_n      = 0;       // queued
static int       s_job_next   = 0;       // the next to start
static int       s_live       = JOB_NONE;
static bool      s_queued     = false;   // sortSourceMaterial() has been called
static bool      s_retry_wait = false;   // a start was refused at s_retry_ms
static uint32_t  s_retry_ms   = 0;

SortData* sortData() { return s_data; }

// ------------------------------------------------------------
//  Small pieces
// ------------------------------------------------------------

static void cutTo(char* out, size_t out_size, const char* s) {
  utf8Cut(s, out_size - 1, out, out_size);
}

// sortEntrySame(), and two empty texts are the same too (no line and no line).
static bool sameText(const char* a, const char* b) {
  return (!a[0] && !b[0]) || sortEntrySame(a, b);
}

static bool isDbRow(const SortRow& r) { return r.from == SF_DB || r.from == SF_DB_OTHER; }

static const char* ownLine(const SortRow& r) { return r.line_known ? r.line : nullptr; }

static uint16_t gramsOf(float grams) {
  if (!(grams > 0.0f)) return 0;
  const long g = lroundf(grams);
  return g > UINT16_MAX ? UINT16_MAX : (uint16_t)g;
}

// "90EE90", "#90ee90", or eight digits with the alpha at the end (Spoolman's
// color_hex, BamBuddy's rgba): the hue as six capitals. Of a list
// ("FF0000,00FF00", Spoolman's multi_color_hexes) the first. Alpha 00 is a
// clear filament, the way Spoolman and BamBuddy keep one (00000000), and
// has none; so has anything unreadable.
static void rowHex(const char* code, char out[7]) {
  out[0] = '\0';
  if (!code) return;
  if (*code == '#') code++;
  const size_t len = strcspn(code, ",");
  if (len != 6 && len != 8) return;
  if (len == 8 && code[6] == '0' && code[7] == '0') return;
  char six[7];
  memcpy(six, code, 6);
  six[6] = '\0';
  sortEntryHex(six, out);   // leaves out empty when it is no hex
}

static bool addRow(const SortRow& r) {
  if (s_data->row_n >= SORT_ROWS_MAX) return false;
  s_data->rows[s_data->row_n++] = r;
  return true;
}

// One line for a list: its count and its first entries, so that the log
// shows what a step offers (the acceptance reads it).
static void logList(const char* what, const char* of, const SortChoices& l) {
  char text[200] = "";
  size_t used = 0;
  for (int i = 0; i < l.n && used + 1 < sizeof(text); i++) {
    const SortChoice& c = l.item[i];
    const int k = snprintf(text + used, sizeof(text) - used, "%s%s%s%s",
                           i ? ", " : "", c.name, c.hex[0] ? " " : "", c.hex);
    if (k < 0) break;
    used += (size_t)k;
  }
  logSDf("SortEntry: %d %s for %s: %s", l.n, what, of, text);
}

// Spoolman's own API at backendFdbBaseUrl(): Spoolman, and the Spoolman
// behind a BamBuddy, straight, as the drying date is written. Otherwise
// BamBuddy's spool list: its own inventory, or its proxy when it has not
// said where its Spoolman is. FilaMan has no entry.
static SortWay inventoryWay() {
  if (backendMode() == BACKEND_SPOOLMAN) return WAY_SPOOLMAN;
  if (backendMode() != BACKEND_BAMBUDDY) return WAY_NONE;
  if (bbInventoryMode() == BB_INV_SPOOLMAN && bbSpoolmanUrl()[0]) return WAY_SPOOLMAN;
  return WAY_BAMBUDDY;
}

// ------------------------------------------------------------
//  Requests
// ------------------------------------------------------------

// A list from Spoolman, filtered. The HTTP code; -2 for an answer that is no list.
static int spoolmanList(const char* path, JsonDocument& doc, JsonDocument& filter) {
  int code = spoolmanGetJson(backendFdbBaseUrl(), path, doc, SORT_HTTP_TIMEOUT_MS, &filter);
  if (code == 200 && !doc.is<JsonArray>()) code = -2;
  return code;
}

static bool readList(Stream& body, void* ctx) {
  ListRead* r = (ListRead*)ctx;
  const DeserializationError err = deserializeJson(*r->doc, body, DeserializationOption::Filter(*r->filter));
  if (err) logSDf("SortEntry: BamBuddy spools did not parse: %s", err.c_str());
  return !err;
}

// BamBuddy's spools, filtered to the keys given. The HTTP code; -2 for an
// answer that is no list.
static int bambuddySpools(JsonDocument& doc, std::initializer_list<const char*> keys) {
  JsonDocument filter;
  JsonObject keep = filter.to<JsonArray>().add<JsonObject>();
  for (const char* key : keys) keep[key] = true;
  char path[64];
  snprintf(path, sizeof(path), "%s/spools", bbInventoryBase());
  ListRead r = { &doc, &filter };
  int code = bbGetStreamed(backendBaseUrl(), bambuddyApiKey(), path, SORT_HTTP_TIMEOUT_MS, readList, &r);
  if (code == 200 && !doc.is<JsonArray>()) code = -2;
  return code;
}

// ------------------------------------------------------------
//  The inventory
// ------------------------------------------------------------

int sortSourceBrands() {
  if (!s_data) return -1;
  HttpStallTime stall(__func__);
  const SortWay way = inventoryWay();
  if (way == WAY_NONE) return BACKEND_NOT_SUPPORTED;
  JsonDocument doc(&s_psram);
  int code;
  if (way == WAY_SPOOLMAN) {
    JsonDocument filter;
    filter.to<JsonArray>().add<JsonObject>()["name"] = true;
    code = spoolmanList(SM_PATH_VENDOR, doc, filter);
  } else {
    code = bambuddySpools(doc, { "brand" });
  }
  if (code == 200) {
    SortChoices* l = &s_data->brands;
    l->n = 0;
    const char* key = way == WAY_SPOOLMAN ? "name" : "brand";
    for (JsonObjectConst v : doc.as<JsonArrayConst>()) sortChoicesAdd(l, v[key] | "", nullptr, SF_OWNED);
  }
  logSDf("SortEntry: %d inventory brands (HTTP %d)", s_data->brands.n, code);
  return code;
}

// The row of the inventory that is this Spoolman filament, -1 for none.
static int ownedRowOf(int filament_id) {
  for (int i = 0; filament_id > 0 && i < s_data->row_n; i++)
    if (s_data->rows[i].from == SF_OWNED && s_data->rows[i].filament_id == filament_id) return i;
  return -1;
}

// The colour name the spools of a filament carry: Bambuddy names the
// filament "<Material> <Line>" ("ABS Basic") and keeps the colour in the
// spool's extra only, so that name replaces the one read from the
// filament's (plan, Festlegung 9). The first spool with one names its
// filament. A failure keeps the names as they are.
static void spoolColourNames(const char* brand, const String& who) {
  JsonDocument filter;
  JsonObject keep = filter.to<JsonArray>().add<JsonObject>();
  keep["filament"]["id"] = true;
  keep["extra"][SM_EXTRA_COLOUR] = true;
  JsonDocument doc(&s_psram);
  const String path = String(SM_PATH_SPOOL) + who;
  const int code = spoolmanList(path.c_str(), doc, filter);
  if (code != 200) {
    logSDf("SortEntry: %s spools failed (HTTP %d), colours from the filament names", brand, code);
    return;
  }
  uint8_t named[(SORT_ROWS_MAX + 7) / 8];
  memset(named, 0, sizeof(named));
  int n = 0, spools = 0;
  char text[SORT_NAME_MAX];
  for (JsonObjectConst s : doc.as<JsonArrayConst>()) {
    spools++;
    if (!sortEntryExtraText(s["extra"][SM_EXTRA_COLOUR] | "", text, sizeof(text))) continue;
    const int at = ownedRowOf(s["filament"]["id"] | 0);
    if (at < 0 || (named[at / 8] & (1u << (at % 8)))) continue;
    named[at / 8] |= (uint8_t)(1u << (at % 8));
    cutTo(s_data->rows[at].name, sizeof(s_data->rows[at].name), text);
    n++;
  }
  logSDf("SortEntry: %s spools, %d colour names from %d spools (HTTP %d)", brand, n, spools, code);
}

// Spoolman: one row per filament of the brand. The line and the colour come
// from the filament's name (sortEntrySplitName); a name that leaves no
// colour ("ASA" of the material ASA) is the colour as a whole. A multi
// colour filament shows its first hue.
static int spoolmanRows(const char* brand) {
  char quoted[SORT_NAME_MAX + 3];   // a brand of the list and its quotes
  snprintf(quoted, sizeof(quoted), "\"%s\"", brand);
  const String who = spoolmanUrlEncode(quoted);
  JsonDocument filter;
  JsonObject keep = filter.to<JsonArray>().add<JsonObject>();
  for (const char* key : { "id", "name", "material", "color_hex", "multi_color_hexes", "weight" }) keep[key] = true;
  keep["vendor"]["name"] = true;
  JsonDocument doc(&s_psram);
  const String path = String(SM_PATH_FILAMENT) + who;
  const int code = spoolmanList(path.c_str(), doc, filter);
  if (code != 200) {
    logSDf("SortEntry: %s filaments failed (HTTP %d)", brand, code);
    return code;
  }
  int n = 0;
  for (JsonObjectConst f : doc.as<JsonArrayConst>()) {
    // A server that passed the filter over would answer every maker's.
    if (!sortEntrySame(f["vendor"]["name"] | "", brand)) continue;
    SortRow r = {};
    cutTo(r.material, sizeof(r.material), f["material"] | "");
    const char* name = f["name"] | "";
    sortEntrySplitName(name, r.material, r.line, r.name);
    if (!r.name[0]) cutTo(r.name, sizeof(r.name), name);
    r.line_known = true;
    rowHex(f["color_hex"] | "", r.hex);
    if (!r.hex[0]) rowHex(f["multi_color_hexes"] | "", r.hex);
    r.grams = gramsOf(f["weight"] | 0.0f);
    r.from = SF_OWNED;
    r.filament_id = f["id"] | 0;
    if (!addRow(r)) { logSDf("SortEntry: %s rows full at %d", brand, SORT_ROWS_MAX); break; }
    n++;
  }
  logSDf("SortEntry: %s inventory, %d filaments (HTTP %d)", brand, n, code);
  doc.clear();
  if (n > 0) spoolColourNames(brand, who);
  return code;
}

// A BamBuddy row with this material, line, colour and hue is there already:
// two spools of one sort are one row.
static bool ownedSortListed(const SortRow& r) {
  for (int i = 0; i < s_data->row_n; i++) {
    const SortRow& x = s_data->rows[i];
    if (x.from == SF_OWNED && sameText(x.material, r.material) && sameText(x.line, r.line) &&
        sameText(x.name, r.name) && strcmp(x.hex, r.hex) == 0) return true;
  }
  return false;
}

// BamBuddy: one row per sort of the brand's spools, the line its subtype.
// No filament behind it: a row carries what a spool carries.
static int bambuddyRows(const char* brand) {
  JsonDocument doc(&s_psram);
  const int code = bambuddySpools(doc, { "brand", "material", "subtype", "color_name", "rgba", "label_weight" });
  if (code != 200) {
    logSDf("SortEntry: %s spools failed (HTTP %d)", brand, code);
    return code;
  }
  int n = 0, spools = 0;
  for (JsonObjectConst s : doc.as<JsonArrayConst>()) {
    if (!sortEntrySame(s["brand"] | "", brand)) continue;
    spools++;
    SortRow r = {};
    cutTo(r.material, sizeof(r.material), s["material"] | "");
    cutTo(r.line, sizeof(r.line), s["subtype"] | "");
    cutTo(r.name, sizeof(r.name), s["color_name"] | "");
    rowHex(s["rgba"] | "", r.hex);
    r.grams = gramsOf(s["label_weight"] | 0.0f);
    r.from = SF_OWNED;
    r.line_known = true;
    if (ownedSortListed(r)) continue;
    if (!addRow(r)) { logSDf("SortEntry: %s rows full at %d", brand, SORT_ROWS_MAX); break; }
    n++;
  }
  logSDf("SortEntry: %s inventory, %d sorts of %d spools (HTTP %d)", brand, n, spools, code);
  return code;
}

// ------------------------------------------------------------
//  The database: the index
// ------------------------------------------------------------

// The maker of the index that is the brand (sortEntrySame), -1 for none and
// when there is no index for this backend.
static int indexMaker(const char* brand) {
  if (!fdbIndexReady()) return -1;
  for (int i = 0; i < fdbMakerCount(); i++) {
    const FdbMaker* m = fdbMaker(i);
    if (m && sortEntrySame(m->name, brand)) return i;
  }
  return -1;
}

static void buildMaterials(const char* brand) {
  SortChoices* l = &s_data->materials;
  l->n = 0;
  for (int i = 0; i < s_data->row_n; i++)
    if (s_data->rows[i].from == SF_OWNED) sortChoicesAdd(l, s_data->rows[i].material, nullptr, SF_OWNED);
  if (fdbIndexReady()) {
    const int mi = indexMaker(brand);
    if (mi >= 0) {
      uint16_t pairs[FDB_PAIRS_MAX];
      const int n = fdbPairsOf(mi, pairs, FDB_PAIRS_MAX);   // most filaments first
      for (int k = 0; k < n; k++) {
        const FdbPair* p = fdbPair(pairs[k]);
        if (p) sortChoicesAdd(l, p->material, nullptr, SF_DB);
      }
    }
    const FdbPair* p;
    for (int i = 0; (p = fdbPair(i)) != nullptr; i++) sortChoicesAdd(l, p->material, nullptr, SF_DB_OTHER);
  } else {
    for (int i = 0; i < SORT_MATERIALS_COUNT; i++) sortChoicesAdd(l, SORT_MATERIALS[i], nullptr, SF_USUAL);
  }
  logList("materials", brand, *l);
}

// Forgets the loads asked for. One of ours still out is dropped when it
// arrives: it belongs to a material or a brand no longer chosen.
static void dropQueue() {
  s_job_n = s_job_next = 0;
  s_queued = false;
  s_retry_wait = false;
  if (s_live >= 0) s_live = JOB_STALE;
}

int sortSourceBrand(const char* brand) {
  if (!s_data || !brand) return -1;
  HttpStallTime stall(__func__);
  s_data->row_n = 0;
  dropQueue();
  const SortWay way = inventoryWay();
  int code = BACKEND_NOT_SUPPORTED;
  if (way == WAY_SPOOLMAN) code = spoolmanRows(brand);
  else if (way == WAY_BAMBUDDY) code = bambuddyRows(brand);
  buildMaterials(brand);
  return code;
}

// ------------------------------------------------------------
//  The database: the loads
// ------------------------------------------------------------

static void queueJob(const char* maker, const char* material, uint8_t from) {
  if (s_job_n >= SORT_JOBS_MAX) return;
  SortJob& j = s_jobs[s_job_n++];
  memset(&j, 0, sizeof(j));
  cutTo(j.maker, sizeof(j.maker), maker);
  cutTo(j.material, sizeof(j.material), material);
  j.from = from;
}

// The database rows of the material chosen before go; the inventory's stay
// where they are (they come first, so their indices do not move).
static void dropDbRows() {
  int n = 0;
  for (int i = 0; i < s_data->row_n; i++)
    if (!isDbRow(s_data->rows[i])) s_data->rows[n++] = s_data->rows[i];
  s_data->row_n = n;
}

void sortSourceMaterial(const char* brand, const char* material) {
  if (!s_data || !brand || !material) return;
  dropDbRows();
  dropQueue();
  s_queued = true;
  const int mi = indexMaker(brand);
  if (mi < 0) {
    logSDf("SortEntry: %s is no maker of the database, nothing to load", brand);
    return;
  }
  const char* maker = fdbMaker(mi)->name;
  uint16_t pairs[FDB_PAIRS_MAX];
  const int n = fdbPairsOf(mi, pairs, FDB_PAIRS_MAX);   // most filaments first
  for (int k = 0; k < n; k++) {
    const FdbPair* p = fdbPair(pairs[k]);
    if (p && sortEntrySame(p->material, material)) { queueJob(maker, p->material, SF_DB); break; }
  }
  int others = 0;
  for (int k = 0; k < n && others < SORT_OTHER_MATERIALS; k++) {
    const FdbPair* p = fdbPair(pairs[k]);
    if (!p || sortEntrySame(p->material, material)) continue;
    queueJob(maker, p->material, SF_DB_OTHER);
    others++;
  }
  logSDf("SortEntry: database %s %s, %d loads queued (%d other materials)", maker, material, s_job_n, others);
}

// The same filament of the database is in the rows already, and so is its
// weight: the next size of one colour adds nothing to any list. Keeps the
// rows for colours (a list of 224 names comes in up to three sizes).
static bool dbRowRedundant(const SortRow& r) {
  bool same_sort = false, same_grams = false;
  for (int i = 0; i < s_data->row_n && !(same_sort && same_grams); i++) {
    const SortRow& x = s_data->rows[i];
    if (!isDbRow(x) || !sameText(x.material, r.material)) continue;
    if (x.line_known == r.line_known && sameText(x.line, r.line) && sameText(x.name, r.name)) same_sort = true;
    if (x.grams == r.grams) same_grams = true;
  }
  return same_sort && same_grams;
}

// The entries of a finished load as rows: the name as the picker shows it,
// the line where the entry has one apart (else line_known stays false and
// the line is the front of the name), a clear one without a hue. Entries of
// several colours are left out: the entry has one hue.
static void copyEntries(const SortJob& j, int code) {
  int added = 0, multi = 0;
  bool full = false;
  char text[FDB_NAME_MAX];
  for (int i = 0; i < fdbEntryCount(); i++) {
    const FdbEntry* e = fdbEntry(i);
    if (!e) break;
    if (e->ncolors >= 2) { multi++; continue; }
    SortRow r = {};
    fdbEntryDisplayName(*e, j.maker, j.material, text, sizeof(text));
    cutTo(r.name, sizeof(r.name), text);
    if (e->line[0]) {
      fdbLineName(*e, "", text, sizeof(text));   // "---other---" gives none
      cutTo(r.line, sizeof(r.line), text);
      r.line_known = r.line[0] != '\0';
    }
    cutTo(r.material, sizeof(r.material), j.material);
    if (e->family != CF_CLEAR) sortEntryHex(e->hex[0], r.hex);
    r.grams = e->weight_g;
    r.from = j.from;
    if (dbRowRedundant(r)) continue;
    if (!addRow(r)) { full = true; break; }
    added++;
  }
  logSDf("SortEntry: database %s %s, %d rows of %d entries, %d multi colour left out%s (HTTP %d)",
         j.maker, j.material, added, fdbEntryCount(), multi, full ? ", rows full" : "", code);
}

static void collectJob() {
  const int code = fdbResultCode();
  const bool current = fdbResultCurrent();
  fdbTake();   // sorts the entries, or frees them after a failure
  const int at = s_live;
  s_live = JOB_NONE;
  if (at >= 0) {
    const SortJob& j = s_jobs[at];
    if (code == 200 && current) copyEntries(j, code);
    else logSDf("SortEntry: database %s %s failed (HTTP %d, current %d)", j.maker, j.material, code, (int)current);
  }
  // Copied or dropped: the PSRAM goes back before the next load.
  fdbReleaseEntries();
}

// filament_db is free, and our load was not collected: dbPickTick() takes
// any finished load, the picker closed or not. Asked for once more; a load
// taken twice is left out.
static void jobVanished() {
  const int at = s_live;
  s_live = JOB_NONE;
  if (at < 0) return;
  SortJob& j = s_jobs[at];
  if (j.restarted) {
    logSDf("SortEntry: database %s %s taken by another tick again, left out", j.maker, j.material);
    return;
  }
  j.restarted = true;
  s_job_next = at;   // loads run one after the other: it was the last started
  logSDf("SortEntry: database %s %s taken by another tick, started again", j.maker, j.material);
}

static void startJob() {
  if (fdbState() != FDB_IDLE) return;   // another load: as soon as it is done
  if (s_retry_wait && millis() - s_retry_ms < SORT_RETRY_MS) return;
  SortJob& j = s_jobs[s_job_next];
  if (fdbStartEntries(j.maker, j.material)) {
    s_live = s_job_next++;
    s_retry_wait = false;
    return;
  }
  s_retry_wait = true;
  s_retry_ms = millis();
  if (++j.refusals < SORT_REFUSALS_MAX) return;
  logSDf("SortEntry: database %s %s refused %d times, left out", j.maker, j.material, (int)j.refusals);
  s_job_next++;
}

SortLoad sortSourceTick() {
  if (!s_data || !s_queued) return SL_IDLE;
  if (s_live != JOB_NONE) {
    const FdbState state = fdbState();
    if (state == FDB_DONE && fdbJob() == FDB_JOB_ENTRIES) collectJob();
    else if (state == FDB_IDLE) jobVanished();
  }
  if (s_live == JOB_NONE && s_job_next < s_job_n) startJob();
  // A stale load still out keeps the next from starting, but is no part of
  // what the step waits for.
  return (s_live >= 0 || s_job_next < s_job_n) ? SL_RUNNING : SL_DONE;
}

// ------------------------------------------------------------
//  The lists after the material
// ------------------------------------------------------------

void sortSourceLines(const char* material) {
  if (!s_data || !material) return;
  SortChoices* l = &s_data->lines;
  l->n = 0;
  // The inventory first, then the database (pass 1), whose line may stand
  // in front of the colour.
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < s_data->row_n; i++) {
      const SortRow& r = s_data->rows[i];
      if (!(pass ? isDbRow(r) : r.from == SF_OWNED) || !sameText(r.material, material)) continue;
      const char* line = r.line_known ? r.line : sortEntryLineOf(r.name);
      if (line) sortChoicesAdd(l, line, nullptr, pass ? SF_DB : SF_OWNED);
    }
  }
  for (int i = 0; i < SORT_LINES_COUNT; i++) sortChoicesAdd(l, SORT_LINES[i], nullptr, SF_USUAL);
  logList("lines", material, *l);
}

// The entry of one filament of the inventory, under a name not taken yet:
// its colour, and for a second filament of the same colour the colour with
// its line, "Light Green (Matte)" (plan, Festlegung 9); where the line does
// not tell them apart, with Spoolman's number or the hue. The colour is cut
// to leave room for what is added. The index of the new entry, -1 when
// there is none (no colour, every name taken, the list full).
static int addOwnedColour(const SortRow& r) {
  if (!r.name[0]) return -1;
  SortChoices* l = &s_data->colours;
  char tail[SORT_LINE_MAX + 16];
  char name[SORT_NAME_MAX];
  for (int k = 0; k < 4; k++) {
    if (k == 0) tail[0] = '\0';
    else if (k == 1 && r.line[0]) snprintf(tail, sizeof(tail), " (%s)", r.line);
    else if (k == 2 && r.filament_id > 0) snprintf(tail, sizeof(tail), " (#%d)", r.filament_id);
    else if (k == 3 && r.hex[0]) snprintf(tail, sizeof(tail), " (%s)", r.hex);
    else continue;
    utf8Cut(r.name, sizeof(name) - 1 - strlen(tail), name, sizeof(name));
    const size_t used = strlen(name);
    snprintf(name + used, sizeof(name) - used, "%s", tail);
    if (sortChoicesFind(*l, name) >= 0) continue;
    return sortChoicesAdd(l, name, r.hex, SF_OWNED);
  }
  return -1;
}

// The colour of a row of another material: one without a line, or with the
// chosen one.
static bool otherColour(const SortRow& r, const char* line, char* out, size_t out_size) {
  return sortEntryColourOf(r.name, ownLine(r), line, out, out_size) ||
         sortEntryColourOf(r.name, ownLine(r), "", out, out_size);
}

enum ColourPass : uint8_t { CP_DB_SAME = 0, CP_DB_OTHER, CP_OWNED_OTHER };

static void addRowColours(const char* material, const char* line, uint8_t pass) {
  static const uint8_t FROM[] = { SF_DB, SF_DB_OTHER, SF_OWNED };
  char colour[SORT_NAME_MAX];
  for (int i = 0; i < s_data->row_n; i++) {
    const SortRow& r = s_data->rows[i];
    const bool same = sameText(r.material, material);
    bool take;
    if (pass == CP_DB_SAME)       take = isDbRow(r) && same && sortEntryColourOf(r.name, ownLine(r), line, colour, sizeof(colour));
    else if (pass == CP_DB_OTHER) take = isDbRow(r) && !same && otherColour(r, line, colour, sizeof(colour));
    else                          take = r.from == SF_OWNED && !same && otherColour(r, line, colour, sizeof(colour));
    if (take) sortChoicesAdd(&s_data->colours, colour, r.hex, FROM[pass]);
  }
}

void sortSourceColours(const char* material, const char* line) {
  if (!s_data || !material) return;
  if (!line) line = "";
  SortChoices* l = &s_data->colours;
  l->n = 0;
  for (int i = 0; i < s_data->row_n; i++) {
    const SortRow& r = s_data->rows[i];
    if (r.from != SF_OWNED || !sameText(r.material, material)) continue;
    const int at = addOwnedColour(r);
    if (at >= 0) l->item[at].row = (int16_t)i;
  }
  addRowColours(material, line, CP_DB_SAME);
  addRowColours(material, line, CP_DB_OTHER);
  addRowColours(material, line, CP_OWNED_OTHER);
  for (int i = 0; i < SORT_COMMON_COUNT; i++) sortChoicesAdd(l, SORT_COMMON[i].name, SORT_COMMON[i].hex, SF_USUAL);
  char of[sizeof(SortRow::material) + SORT_LINE_MAX + 1];
  snprintf(of, sizeof(of), "%s%s%s", material, line[0] ? " " : "", line);
  logList("colours", of, *l);
}

void sortSourceWeights(const char* material) {
  if (!s_data || !material) return;
  SortChoices* l = &s_data->weights;
  l->n = 0;
  // The database first, then the inventory (pass 1).
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < s_data->row_n; i++) {
      const SortRow& r = s_data->rows[i];
      if ((pass ? r.from == SF_OWNED : isDbRow(r)) && sameText(r.material, material))
        sortChoicesAddGrams(l, r.grams, pass ? SF_OWNED : SF_DB);
    }
  }
  for (int i = 0; i < SORT_SIZES_COUNT; i++) sortChoicesAddGrams(l, SORT_SIZES[i], SF_USUAL);
  logList("weights", material, *l);
}

// ------------------------------------------------------------
//  Opening and closing
// ------------------------------------------------------------

void sortSourceEnd() {
  heap_caps_free(s_data);
  s_data = nullptr;
  dropQueue();
  // A load of ours still out is collected by dbPickTick(), which drops it
  // with the picker closed.
  s_live = JOB_NONE;
  fdbReleaseEntries();
}

bool sortSourceBegin() {
  sortSourceEnd();
  s_data = (SortData*)heap_caps_calloc(1, sizeof(SortData), MALLOC_CAP_SPIRAM);
  if (!s_data) {
    logSDf("SortEntry: no PSRAM for the lists (%u bytes)", (unsigned)sizeof(SortData));
    return false;
  }
  return true;
}
