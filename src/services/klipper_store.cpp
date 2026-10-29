#include "services/klipper_store.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <nvs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/sd_logger.h"
#include "services/klipper_rules.h"
#include "services/prefs_store.h"

// NVS keys, at most 15 characters. The record of slot N is "kl_p<N>", N from
// 0 to KL_PRINTERS_MAX - 1: "kl_p63" is 6 characters.
#define NVS_KL_RECORD  "kl_p%d"
#define NVS_KL_KEYS    "kl_keys"
#define NVS_KL_NEXT    "kl_next"
#define NVS_KL_LOC     "kl_loc"

// ------------------------------------------------------------
//  The cache
// ------------------------------------------------------------

// Everything read from NVS, in one block of PSRAM (about 13 kB): the internal
// RAM keeps a pointer and three numbers, a few dozen bytes. The block is made
// once, on the first load, and never freed: the printers are there for as
// long as the scale runs.
struct KlipperStoreCache {
  KlipperPrinterCfg printer[KL_PRINTERS_MAX];   // by id, ascending; the first `count` are in use
  uint8_t           slot[KL_PRINTERS_MAX];      // the NVS slot (kl_pN) of printer[i]
  uint64_t          taken;                      // bit N: slot N holds something, see isFreeText()
  int               count;
  char              keys[KL_KEYS_MAX];          // the list of keys as it is stored
  char              spare[KL_KEYS_MAX];         // where klipperKeysPut() builds the next list: it may not write over its input (R17)
};

static KlipperStoreCache* s_c = nullptr;
static uint32_t s_next      = 0;       // kl_next: the next id to give out, as stored
static uint32_t s_available = 0;       // available entries at the last load, for when nvs_get_stats() fails
static bool     s_loc       = true;    // "storage location = printer name", on from the factory

// PSRAM first, internal memory when the board has none or it is full.
static KlipperStoreCache* cacheGet() {
  if (s_c) return s_c;
  void* mem = heap_caps_calloc(1, sizeof(KlipperStoreCache), MALLOC_CAP_SPIRAM);
  if (!mem) mem = calloc(1, sizeof(KlipperStoreCache));
  s_c = (KlipperStoreCache*)mem;
  return s_c;
}

static void slotName(int slot, char* out, size_t n) {
  snprintf(out, n, NVS_KL_RECORD, slot);
}

// A write that did not take, named by its NVS key (never by a value).
// prefs_store logs a failure only once a minute; this line is for the
// operation that gave up because of it.
static void writeFailed(const char* nvs_name) {
  logSDf("KLIPPER: write of %s failed", nvs_name);
}

// R22: a deleted printer's slot is written as "", because prefs_store has no
// way to remove a key (its parked writes would need one as well). A slot that
// was never used and one that was emptied look the same to the loader: both
// are free. Anything else in a slot is taken, whether it decodes or not.
static bool isFreeText(const String& text) {
  return text.length() == 0;
}

static int indexOf(const KlipperStoreCache* s, uint32_t id) {
  for (int i = 0; i < s->count; i++) {
    if (s->printer[i].id == id) return i;
  }
  return -1;
}

// The first slot that holds nothing, -1 when all of them are taken.
static int freeSlot(const KlipperStoreCache* s) {
  for (int slot = 0; slot < KL_PRINTERS_MAX; slot++) {
    if ((s->taken & (1ULL << slot)) == 0) return slot;
  }
  return -1;
}

// Puts a printer where its id belongs. The array has a place for every slot,
// so this cannot run past its end.
static void insertSorted(KlipperStoreCache* s, const KlipperPrinterCfg& p, int slot) {
  if (s->count >= KL_PRINTERS_MAX) return;
  int i = s->count;
  while (i > 0 && s->printer[i - 1].id > p.id) {
    s->printer[i] = s->printer[i - 1];
    s->slot[i]    = s->slot[i - 1];
    i--;
  }
  s->printer[i] = p;
  s->slot[i]    = (uint8_t)slot;
  s->count++;
  s->taken |= (1ULL << slot);
}

static void removeAt(KlipperStoreCache* s, int index) {
  s->taken &= ~(1ULL << s->slot[index]);
  for (int i = index; i + 1 < s->count; i++) {
    s->printer[i] = s->printer[i + 1];
    s->slot[i]    = s->slot[i + 1];
  }
  s->count--;
}

// ------------------------------------------------------------
//  Load
// ------------------------------------------------------------

// R18: the decoder accepts any number from 0 to 255 for the test and the
// layout, and the screens index string tables with them. What the cache
// hands out is in range, so that a record written by another version, or
// edited by hand in a backup, cannot reach past a table.
static void clampOutcome(KlipperPrinterCfg* p, const char* slot_name) {
  if (p->last.test <= KT_BAD_ANSWER && p->last.layout <= KL_LAYOUT_AFC) return;
  logSDf("KLIPPER: %s has a test outcome out of range, shown as never tested", slot_name);
  p->last = KlipperTestOutcome{};
}

static void loadRecords(KlipperStoreCache* s) {
  for (int slot = 0; slot < KL_PRINTERS_MAX; slot++) {
    char name[16];
    slotName(slot, name, sizeof(name));
    const String text = prefsGetString(name, "");
    if (isFreeText(text)) continue;
    s->taken |= (1ULL << slot);   // taken even when it does not decode: not ours to overwrite

    KlipperPrinterCfg p;
    if (text.length() >= KL_RECORD_MAX || !klipperRecordDecode(text.c_str(), &p)) {
      logSDf("KLIPPER: %s does not decode, skipped", name);
      continue;
    }
    if (indexOf(s, p.id) >= 0) {
      logSDf("KLIPPER: %s repeats printer id %lu, skipped", name, (unsigned long)p.id);
      continue;
    }
    clampOutcome(&p, name);
    insertSorted(s, p, slot);
  }
}

static void loadKeys(KlipperStoreCache* s) {
  const String list = prefsGetString(NVS_KL_KEYS, "");
  if (list.length() >= KL_KEYS_MAX) {
    // Not something this unit wrote (klipperKeysPut refuses a list that does
    // not fit). A cut list would hand a printer the wrong key; none is safer.
    logSD("KLIPPER: the list of keys is too long, none is used");
    s->keys[0] = '\0';
    return;
  }
  memcpy(s->keys, list.c_str(), list.length() + 1);
}

// R24: ids are never used twice, and the counter in flash can be behind the
// printers (a write of kl_next that was lost, a backup restored without it).
// The next id is the largest of the counter, one more than the largest id
// loaded, and the first id; when a printer shows the counter is behind, the
// counter is corrected in flash, so that deleting that printer later cannot
// give its id out again. With no printer there is nothing to protect and
// nothing is written.
static void loadNext(const KlipperStoreCache* s, uint32_t stored) {
  uint32_t ids[KL_PRINTERS_MAX];
  for (int i = 0; i < s->count; i++) ids[i] = s->printer[i].id;
  s_next = klipperNextId(stored, ids, s->count);
  if (s->count > 0 && s_next > stored) {
    logSDf("KLIPPER: id counter was %lu, set to %lu", (unsigned long)stored, (unsigned long)s_next);
    if (!prefsPutUInt(NVS_KL_NEXT, s_next)) writeFailed(NVS_KL_NEXT);
  }
}

void klipperStoreLoad() {
  s_loc = prefsGetBool(NVS_KL_LOC, true);
  const uint32_t stored_next = prefsGetUInt(NVS_KL_NEXT, 0);
  s_next = stored_next;

  KlipperStoreCache* s = cacheGet();
  if (s) {
    memset(s, 0, sizeof(*s));
    loadRecords(s);
    loadKeys(s);
    loadNext(s, stored_next);
  } else {
    logSD("KLIPPER: no memory for the printers, none are loaded");
  }

  // available_entries (ESP-IDF 5.x, nvs.h) is what is left for data;
  // free_entries also counts the entries the partition keeps in reserve.
  nvs_stats_t st = {};
  if (nvs_get_stats(NULL, &st) != ESP_OK) {
    st = nvs_stats_t{};
    logSD("KLIPPER: the settings memory did not report its use");
  }
  s_available = (uint32_t)st.available_entries;
  logSDf("NVS: %u of %u entries used, %u available, %d Klipper printer(s)",
         (unsigned)st.used_entries, (unsigned)st.total_entries,
         (unsigned)st.available_entries, klipperStoreCount());
}

// ------------------------------------------------------------
//  Reading
// ------------------------------------------------------------

int klipperStoreCount() {
  return s_c ? s_c->count : 0;
}

bool klipperStoreAt(int index, KlipperPrinterCfg* out) {
  if (!s_c || !out || index < 0 || index >= s_c->count) return false;
  *out = s_c->printer[index];
  return true;
}

bool klipperStoreFind(uint32_t id, KlipperPrinterCfg* out) {
  if (!s_c || !out) return false;
  const int i = indexOf(s_c, id);
  if (i < 0) return false;
  *out = s_c->printer[i];
  return true;
}

bool klipperStoreKey(uint32_t id, char* out, size_t n) {
  if (!out || n == 0) return false;
  out[0] = '\0';
  if (!s_c || indexOf(s_c, id) < 0) return false;   // a key of no printer is not handed out
  return klipperKeysGet(s_c->keys, id, out, n);
}

int klipperStoreRoomLeft() {
  // Asked again each time: every printer stored takes entries away.
  nvs_stats_t st = {};
  const uint32_t available =
      nvs_get_stats(NULL, &st) == ESP_OK ? (uint32_t)st.available_entries : s_available;
  return klipperRoomLeft(klipperStoreCount(), available);
}

bool klipperLocSwitch() {
  return s_loc;
}

void klipperSetLocSwitch(bool on) {
  s_loc = on;
  const bool stored = prefsPutBool(NVS_KL_LOC, on);
  logSDf("KLIPPER: storage location = printer name: %s%s", on ? "on" : "off",
         stored ? "" : " (not stored)");
}

// ------------------------------------------------------------
//  Save
// ------------------------------------------------------------

// A printer as it is to be stored, judged but not yet written.
struct KlipperDraft {
  KlipperPrinterCfg cfg;       // cleaned; id is 0 until a new printer is given one
  char              key[KL_KEY_MAX];
  bool              set_key;   // key was given (an empty one takes the key away)
  int               index;     // place in the cache, -1 for a new printer
  int               slot;      // NVS slot
};

// The new id of a printer that is added. False when the counter has run out.
static bool newId(const KlipperStoreCache* s, uint32_t* id) {
  uint32_t ids[KL_PRINTERS_MAX];
  for (int i = 0; i < s->count; i++) ids[i] = s->printer[i].id;
  *id = klipperNextId(s_next, ids, s->count);
  return *id != UINT32_MAX;   // the counter after it could not be kept
}

// What a new printer needs on top of the rest: a slot, an id, room.
static KlipperSave placeNew(const KlipperStoreCache* s, KlipperDraft* d) {
  if (klipperStoreRoomLeft() <= 0) return KLS_FULL;
  d->slot = freeSlot(s);
  if (d->slot < 0) return KLS_FULL;
  if (!newId(s, &d->cfg.id)) return KLS_FULL;
  return KLS_OK;
}

// Cleans what was typed and judges it. Nothing is written here.
static KlipperSave prepare(const KlipperStoreCache* s, const KlipperPrinterCfg& in, const char* key, KlipperDraft* d) {
  *d = KlipperDraft{};
  d->index = -1;
  if (in.id != 0) {
    d->index = indexOf(s, in.id);
    if (d->index < 0) return KLS_NOT_FOUND;
    d->cfg  = s->printer[d->index];   // id and the outcome of the last test come from the store
    d->slot = s->slot[d->index];
  }

  char name[KL_NAME_MAX] = {};
  char host[KL_HOST_MAX] = {};
  if (!klipperNameClean(in.name, name, sizeof(name))) return KLS_BAD_NAME;
  if (!klipperHostClean(in.host, host, sizeof(host))) return KLS_BAD_HOST;
  // R24: a new printer always writes its entry in the list of keys, "" when it
  // was given none, so that a key left behind under the same id (a take-back
  // that failed) can never become its key. d->key is "" from the line above.
  d->set_key = (key != nullptr) || d->index < 0;
  if (key != nullptr && !klipperKeyClean(key, d->key, sizeof(d->key))) return KLS_BAD_KEY;

  const char* names[KL_PRINTERS_MAX];
  for (int i = 0; i < s->count; i++) names[i] = s->printer[i].name;
  if (klipperNameTaken(name, names, s->count, d->index)) return KLS_NAME_TAKEN;

  const uint16_t port = in.port != 0 ? in.port : (uint16_t)KL_PORT_DEFAULT;
  if (d->index >= 0 && (port != d->cfg.port || strcmp(host, d->cfg.host) != 0)) {
    d->cfg.last = KlipperTestOutcome{};   // the outcome was about the other address
  }
  memcpy(d->cfg.name, name, sizeof(name));
  memcpy(d->cfg.host, host, sizeof(host));
  d->cfg.port = port;

  return d->index < 0 ? placeNew(s, d) : KLS_OK;
}

// The printer in the cache as it is in flash: replaced, or added.
static void adopt(KlipperStoreCache* s, const KlipperDraft& d) {
  if (d.index >= 0) {
    s->printer[d.index] = d.cfg;
  } else {
    insertSorted(s, d.cfg, d.slot);
  }
}

// Writes a judged printer. The order is: the counter (a number burnt is
// harmless), the record, then the list of keys. A list that does not fit is
// found before anything is written.
static KlipperSave commit(KlipperStoreCache* s, KlipperDraft* d) {
  const bool added = d->index < 0;
  char slot_name[16];
  slotName(d->slot, slot_name, sizeof(slot_name));
  char record[KL_RECORD_MAX];
  if (!klipperRecordEncode(d->cfg, record, sizeof(record))) {
    logSDf("KLIPPER: the record for %s could not be made", slot_name);
    return KLS_WRITE_FAILED;
  }

  bool write_keys = false;
  if (d->set_key) {
    if (!klipperKeysPut(s->keys, d->cfg.id, d->key, s->spare, KL_KEYS_MAX)) return KLS_FULL;
    write_keys = strcmp(s->keys, s->spare) != 0;
  }

  if (added) {
    if (!prefsPutUInt(NVS_KL_NEXT, d->cfg.id + 1)) {
      writeFailed(NVS_KL_NEXT);
      return KLS_WRITE_FAILED;
    }
    s_next = d->cfg.id + 1;
  }
  if (!prefsPutString(slot_name, record)) {
    writeFailed(slot_name);
    return KLS_WRITE_FAILED;
  }
  if (write_keys && !prefsPutString(NVS_KL_KEYS, s->spare)) {
    writeFailed(NVS_KL_KEYS);
    // A new printer is taken back, so that none is left without the key it
    // was typed in with. A changed one is stored as asked, its key as before.
    if (added) {
      if (!prefsPutString(slot_name, "")) logSDf("KLIPPER: could not take back %s", slot_name);
    } else {
      adopt(s, *d);
    }
    return KLS_WRITE_FAILED;
  }
  if (write_keys) memcpy(s->keys, s->spare, strlen(s->spare) + 1);
  adopt(s, *d);
  return KLS_OK;
}

KlipperSave klipperStoreSave(KlipperPrinterCfg* c, const char* key) {
  KlipperStoreCache* s = s_c;
  if (!s) {
    logSD("KLIPPER: save refused, the printers have no memory");
    return KLS_WRITE_FAILED;
  }
  if (!c) return KLS_BAD_NAME;

  KlipperDraft d;
  KlipperSave r = prepare(s, *c, key, &d);
  if (r != KLS_OK) return r;
  r = commit(s, &d);
  if (r != KLS_OK) return r;

  // Only whether there is a key is said, never the key.
  char probe[KL_KEY_MAX];
  const bool has_key = klipperKeysGet(s->keys, d.cfg.id, probe, sizeof(probe));
  logSDf("KLIPPER: printer %lu \"%s\" %s at %s:%u in slot %d, key %s",
         (unsigned long)d.cfg.id, d.cfg.name, d.index < 0 ? "added" : "changed",
         d.cfg.host, (unsigned)d.cfg.port, d.slot, has_key ? "set" : "empty");
  *c = d.cfg;
  return KLS_OK;
}

// ------------------------------------------------------------
//  Delete, test outcome
// ------------------------------------------------------------

// R24: the counter in flash is above this id before the printer with it goes.
// It normally is (a printer's id was given out from the counter); when it is
// not, the printer is the only record that the id was used, and with the
// printer gone the id would be given out again. False when the write did not
// take.
static bool keepNextAbove(uint32_t id) {
  if (s_next > id) return true;
  const uint32_t want = id < UINT32_MAX ? id + 1 : id;
  if (!prefsPutUInt(NVS_KL_NEXT, want)) {
    writeFailed(NVS_KL_NEXT);
    return false;
  }
  s_next = want;
  return true;
}

bool klipperStoreDelete(uint32_t id) {
  KlipperStoreCache* s = s_c;
  if (!s) return false;
  const int index = indexOf(s, id);
  if (index < 0) return false;
  if (!keepNextAbove(id)) return false;

  // The key goes first: a printer left without its key can be given one
  // again, a key left without its printer would stay in flash unseen.
  if (!klipperKeysPut(s->keys, id, "", s->spare, KL_KEYS_MAX)) return false;
  if (strcmp(s->keys, s->spare) != 0) {
    if (!prefsPutString(NVS_KL_KEYS, s->spare)) {
      writeFailed(NVS_KL_KEYS);
      return false;
    }
    memcpy(s->keys, s->spare, strlen(s->spare) + 1);
  }

  char slot_name[16];
  slotName(s->slot[index], slot_name, sizeof(slot_name));
  if (!prefsPutString(slot_name, "")) {   // R22: emptied, not removed
    writeFailed(slot_name);
    return false;
  }
  removeAt(s, index);
  logSDf("KLIPPER: printer %lu deleted from %s", (unsigned long)id, slot_name);
  return true;
}

bool klipperStoreSetTest(uint32_t id, const KlipperTestOutcome& o) {
  KlipperStoreCache* s = s_c;
  if (!s) return false;
  const int index = indexOf(s, id);
  if (index < 0) return false;
  if (o.test > KT_BAD_ANSWER || o.layout > KL_LAYOUT_AFC) return false;   // R18

  KlipperPrinterCfg next = s->printer[index];
  const KlipperTestOutcome& had = next.last;
  if (had.test == o.test && had.layout == o.layout && had.fields == o.fields &&
      had.flags == o.flags && had.at == o.at) {
    return true;   // flash is written as seldom as it can be
  }
  next.last = o;

  char slot_name[16];
  slotName(s->slot[index], slot_name, sizeof(slot_name));
  char record[KL_RECORD_MAX];
  if (!klipperRecordEncode(next, record, sizeof(record))) {
    logSDf("KLIPPER: the record for %s could not be made", slot_name);
    return false;
  }
  if (!prefsPutString(slot_name, record)) {
    writeFailed(slot_name);
    return false;
  }
  s->printer[index] = next;
  return true;
}
