#include "services/klipper_location.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/backend_api.h"
#include "services/klipper_location_rules.h"
#include "services/klipper_printers.h"
#include "services/klipper_store.h"
#include "services/moonraker_api.h"
#include "services/spoolman_api.h"

namespace {

// ArduinoJson has to be told to use PSRAM, and the allocator must be defined in
// every translation unit that needs it (same as moonraker_api.cpp).
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

// Each request to Spoolman, as long as the location write before V50 waited.
#define KL_LOC_MS 8000

// What one call needs besides the stack, about 3 kB: the names of the stored
// printers for klipperPrevToRemember(), and the place of the spool read last.
struct LocWork {
  char names[KL_PRINTERS_MAX][KL_NAME_MAX];
  const char* name_of[KL_PRINTERS_MAX];
  int name_count;
  char location[KL_LOC_MAX];        // the spool's location in Spoolman
  char prev[KL_LOC_MAX];            // its remembered one, "" for none
  char remember[KL_LOC_MAX];        // what the assigned spool remembers now
  char text[KL_LOC_TEXT_MAX];       // that as the JSON text of the extra field
};

class LocBlock {
 public:
  LocBlock() : block_(static_cast<LocWork*>(moonrakerAlloc(sizeof(LocWork)))) {}
  ~LocBlock() { moonrakerFree(block_); }
  LocBlock(const LocBlock&) = delete;
  LocBlock& operator=(const LocBlock&) = delete;
  LocWork* get() const { return block_; }

 private:
  LocWork* block_;
};

// The whole text or nothing: a location cut short would be another one.
bool copyWhole(char* dst, size_t n, const char* src) {
  const size_t len = strlen(src);
  if (len >= n) {
    dst[0] = '\0';
    return false;
  }
  memcpy(dst, src, len + 1);
  return true;
}

void loadNames(LocWork* w) {
  w->name_count = 0;
  const int count = klipperStoreCount();
  for (int i = 0; i < count && w->name_count < KL_PRINTERS_MAX; i++) {
    KlipperPrinterCfg cfg;
    if (!klipperStoreAt(i, &cfg)) continue;
    char* dst = w->names[w->name_count];
    memcpy(dst, cfg.name, KL_NAME_MAX);
    dst[KL_NAME_MAX - 1] = '\0';
    w->name_of[w->name_count] = dst;
    w->name_count++;
  }
}

// How the assigned spool's write came out: the location alone and what the
// displaced spool gets are decided on it.
enum PlaceAns : uint8_t {
  PA_PLACED = 0,   // location and old location in one PATCH
  PA_UNREAD,       // not read, or its old location does not fit: the field untried
  PA_NO_FIELD,     // the field could not be made, or the PATCH with it failed
  PA_GONE,         // Spoolman did not answer the read at all
};

// The spool's location and its remembered one as Spoolman has them now, read
// fresh rather than trusted from the scale's memory: GET /api/v1/spool/<id>,
// only these two kept. 200, the code of the request, or -2 for an answer that
// does not parse or a location that does not fit. *gone (when asked): no
// answer at all, a code <= 0 of the request itself; spoolmanGetJson() answers
// -2 for a parse error too, and that Spoolman is there. A remembered value
// that cannot be read counts as none.
int readPlace(const char* base, int spool_id, LocWork* w, bool* gone) {
  w->location[0] = '\0';
  w->prev[0] = '\0';
  char path[32];
  snprintf(path, sizeof(path), "/api/v1/spool/%d", spool_id);
  JsonDocument filter;
  filter["location"] = true;
  filter["extra"][KL_PREV_LOC_KEY] = true;
  SpiRamAllocator alloc;
  JsonDocument doc(&alloc);
  DeserializationError err = DeserializationError::Ok;
  const int code = spoolmanGetJson(base, path, doc, KL_LOC_MS, &filter, &err);
  if (gone) *gone = code <= 0 && !err;
  if (code != 200) return code;
  if (!copyWhole(w->location, sizeof(w->location), doc["location"] | "")) {
    logSDf("KLIPPER: the location of spool %d is longer than %d bytes", spool_id, KL_LOC_MAX - 1);
    return -2;
  }
  const char* text = doc["extra"][KL_PREV_LOC_KEY] | "";
  if (text[0] && !klipperExtraValue(text, w->prev, sizeof(w->prev))) {
    logSDf("KLIPPER: the old location of spool %d in Spoolman is no JSON text or longer than %d bytes, taken as none",
           spool_id, KL_LOC_MAX - 1);
  }
  return 200;
}

// The body of one PATCH of the location and the extra field together. A null
// location is no location, a null text clears the field (Spoolman 0.26 on).
String placeBody(const char* location, const char* text) {
  SpiRamAllocator alloc;
  JsonDocument body(&alloc);
  if (location) {
    body["location"] = location;
  } else {
    body["location"] = nullptr;
  }
  if (text) {
    body["extra"][KL_PREV_LOC_KEY] = text;
  } else {
    body["extra"][KL_PREV_LOC_KEY] = nullptr;
  }
  String payload;
  serializeJson(body, payload);
  return payload;
}

// The field, text, named with the scale's text in the language that is set.
// Spoolman's POST makes it, or names it again when it is there already.
int makeField(const char* base) {
  SpiRamAllocator alloc;
  JsonDocument body(&alloc);
  body["name"] = T(STR_KL_PREV_LOC_FIELD);
  body["field_type"] = "text";
  String payload;
  serializeJson(body, payload);
  JsonDocument answer(&alloc);   // the list of all spool fields; the code is what counts
  return spoolmanPostJson(base, "/api/v1/field/spool/" KL_PREV_LOC_KEY, payload, answer, KL_LOC_MS);
}

// Root: Spoolman refuses an extra key it has no field for with 400, and that
//   400 throws away the whole PATCH, the location with it.
// Fix: on that answer the field is made and the same PATCH sent exactly once
//   more, as backendPatchFilamentDrying() does for the filament's field. Every
//   later write costs nothing extra.
// NOT-TO-DO: asking GET /api/v1/field/spool first on every assignment, or
//   retrying in a loop: a second 400 has another cause, and the caller falls
//   back to the location alone.
int patchPlace(const char* base, int spool_id, const String& payload) {
  const int code = spoolmanPatchSpool(base, spool_id, payload, KL_LOC_MS);
  if (code != 400) return code;
  const int made = makeField(base);
  const bool again = made == 200 || made == 201;
  logSDf("KLIPPER: write of spool %d refused (HTTP 400), field 'scale_prev_location' created: HTTP %d, %s",
         spool_id, made, again ? "sent once more" : "not sent again");
  if (!again) return code;
  return spoolmanPatchSpool(base, spool_id, payload, KL_LOC_MS);
}

// The assigned spool: location = the printer's name and, in the same PATCH,
// what it remembers.
PlaceAns placeWithPrev(const char* base, int spool_id, const char* printer_name, LocWork* w) {
  bool gone = false;
  const int read = readPlace(base, spool_id, w, &gone);
  if (gone) {
    logSDf("KLIPPER: Spoolman did not answer for spool %d (%d): nothing more sent, no location written, "
           "the displaced spool not asked", spool_id, read);
    return PA_GONE;
  }
  if (read != 200) {
    logSDf("KLIPPER: spool %d not read from Spoolman (%d), its old location is not remembered", spool_id, read);
    return PA_UNREAD;
  }
  loadNames(w);
  const KlipperNames printers = {w->name_of, w->name_count};
  if (!klipperPrevToRemember(w->location, w->prev, printers, w->remember, sizeof(w->remember)) ||
      !klipperExtraText(w->remember, w->text, sizeof(w->text))) {
    logSDf("KLIPPER: the old location of spool %d does not fit the extra field", spool_id);
    return PA_UNREAD;
  }
  const int code = patchPlace(base, spool_id, placeBody(printer_name, w->text));
  if (!backendWriteOk(code)) {
    logSDf("KLIPPER: location of spool %d with its old one not written, HTTP %d", spool_id, code);
    return PA_NO_FIELD;
  }
  logSDf("KLIPPER: location of spool %d -> '%s' (was '%s' remembered)", spool_id, printer_name, w->remember);
  return PA_PLACED;
}

// The spool that stood in the field before the commands. field_ok false: the
// field did not work for the assigned spool, so nothing is given back and the
// location is only cleared, on its own, while it is the printer's name.
void giveBack(const char* base, int spool_id, const char* printer_name, bool field_ok, LocWork* w) {
  const int read = readPlace(base, spool_id, w, nullptr);
  if (read != 200) {
    logSDf("KLIPPER: displaced spool %d not read from Spoolman (%d), its location stays", spool_id, read);
    return;
  }
  const KlipperRestoreAct act = klipperRestoreFor(w->location, printer_name, field_ok ? w->prev : "");
  if (act == KR_NONE) {
    logSDf("KLIPPER: displaced spool %d is at '%s', not at '%s': left as it is", spool_id,
           w->location, printer_name);
    return;
  }
  if (field_ok) {
    const int code = patchPlace(base, spool_id, placeBody(act == KR_RESTORE ? w->prev : nullptr, nullptr));
    if (backendWriteOk(code)) {
      if (act == KR_CLEAR) {
        logSDf("KLIPPER: spool %d left %s, nothing remembered: location cleared", spool_id, printer_name);
      } else {
        logSDf("KLIPPER: spool %d back to '%s'", spool_id, w->prev);
      }
      return;
    }
    logSDf("KLIPPER: displaced spool %d not given back, HTTP %d", spool_id, code);
  }
  const int code = spoolmanPatchSpoolLocation(base, spool_id, "", KL_LOC_MS);
  logSDf("KLIPPER: displaced spool %d: location '%s' cleared alone, HTTP %d", spool_id, printer_name, code);
}

}  // namespace

// Root: at the acceptance of v0.8.6-beta.8 spool 18 went into the Klipper
//   printer "Hulk" (location "Hulk") and was displaced by spool 14; it kept
//   "Hulk", and nothing knew it had come from "Regal".
// Fix: the old location goes into the spool's extra field scale_prev_location
//   in the same PATCH that sets the printer's name, and the displaced spool
//   gets it back, read fresh from Spoolman and only while its location is
//   still exactly the printer's name (klipper_location_rules.h).
// NOT-TO-DO: keeping the old location in the scale's settings memory: a
//   second scale or a factory reset loses it, and Spoolman would not show it.
//   Writing through backendPatchSpoolLocation(): in Bambuddy mode that is the
//   proxy, which drops the extra field, and two requests can land half.
//   Restoring a spool whose location is no longer the printer's name: the user
//   has put it somewhere else since.
// See test_klipper_location_rules.py and test_klipper_wiring.py, section 7.
bool klipperLocationAssigned(int spool_id, int displaced_id, const char* printer_name) {
  const char* base = klipperScaleSpoolman();
  if (spool_id <= 0 || !printer_name || !printer_name[0] || !base[0]) {
    logSDf("KLIPPER: location of spool %d not written: the scale has no Spoolman for it", spool_id);
    return false;
  }
  const LocBlock block;
  LocWork* w = block.get();
  PlaceAns ans = PA_UNREAD;   // no memory: the field untried, the location goes alone
  if (w) {
    ans = placeWithPrev(base, spool_id, printer_name, w);
  } else {
    logSD("KLIPPER: no memory to remember the old location");
  }
  // Root: with Spoolman gone, the new spool's read, the location alone and the
  //   displaced spool's read each waited out KL_LOC_MS: the screen stood 15-24 s.
  // Fix: no answer at all to the first request ends the call (logged there).
  // NOT-TO-DO: taking any code <= 0 as gone: -2 is also an answer that did not
  //   parse (readPlace()), and that Spoolman takes the location alone.
  if (ans == PA_GONE) return false;
  bool placed = ans == PA_PLACED;
  if (!placed) {
    const int code = spoolmanPatchSpoolLocation(base, spool_id, printer_name, KL_LOC_MS);
    logSDf("KLIPPER: location of spool %d -> '%s' alone, HTTP %d", spool_id, printer_name, code);
    placed = backendWriteOk(code);
  }
  // Root: a new spool that was not read (or whose old location did not fit)
  //   counted as a field that does not work: the displaced spool was only
  //   cleared, and its remembered "Regal" was lost.
  // Fix: only PA_NO_FIELD (the field's POST or the PATCH with it failed) clears alone.
  // NOT-TO-DO: leaving the displaced spool alone then: it would keep the printer's name.
  if (w && displaced_id > 0 && displaced_id != spool_id) {
    giveBack(base, displaced_id, printer_name, ans != PA_NO_FIELD, w);
  }
  return placed;
}
