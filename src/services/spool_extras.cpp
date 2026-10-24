#include "services/spool_extras.h"

#include <Arduino.h>
#include <stdio.h>

#include "app/app_state.h"
#include "services/prefs_store.h"

// The part of the parts that keeps things: the list and the choices in NVS,
// and the cache of the last choice asked for. What a mask means, what it
// weighs, what a name or a weight may be and what is stored for a choice all
// come from spool_extras_rules, which a host test holds to its values.

// NVS: "xtr_n<i>" and "xtr_g<i>" per place, "xtr_def" the default, and
// "xs<id>" a spool's own choice. SPOOL_EXTRAS_FOLLOW_DEFAULT (0xFF) there
// means "the default": NVS here has no erase, and a spool back on the default
// must follow it when the default changes later.

static SpoolExtra s_list[SPOOL_EXTRAS_MAX];
static uint8_t    s_default = 0;
// The last spool asked about. The main screen asks on every loop pass, and
// each NVS read opens the namespace.
static int        s_cached_id   = -1;
static uint8_t    s_cached_mask = 0;

static void keyFor(char* out, size_t n, const char* prefix, int i) {
  snprintf(out, n, "%s%d", prefix, i);
}

void spoolExtrasLoad() {
  char key[12];
  for (int i = 0; i < SPOOL_EXTRAS_MAX; i++) {
    keyFor(key, sizeof(key), "xtr_n", i);
    extrasTrimName(s_list[i].name, sizeof(s_list[i].name), prefsGetString(key, "").c_str());
    // Every weight through the cut here: the rules add up what the list holds.
    keyFor(key, sizeof(key), "xtr_g", i);
    s_list[i].grams = extrasClampGrams(prefsGetFloat(key, 0.0f));
  }
  s_default = extrasClampMask(s_list, prefsGetUChar("xtr_def", 0));
  s_cached_id = -1;
}

const SpoolExtra& spoolExtraAt(int i) {
  static const SpoolExtra none{};
  return (i >= 0 && i < SPOOL_EXTRAS_MAX) ? s_list[i] : none;
}

bool spoolExtraUsed(int i) {
  return extrasPlaceUsed(s_list, i);
}

bool spoolExtrasAny() {
  for (int i = 0; i < SPOOL_EXTRAS_MAX; i++)
    if (spoolExtraUsed(i)) return true;
  return false;
}

uint8_t spoolExtrasClamp(uint8_t mask) {
  return extrasClampMask(s_list, mask);
}

bool spoolExtrasSave(const SpoolExtra* list, uint8_t default_mask) {
  bool ok = true;
  char key[12];
  for (int i = 0; i < SPOOL_EXTRAS_MAX; i++) {
    extrasTrimName(s_list[i].name, sizeof(s_list[i].name), list[i].name);
    s_list[i].grams = s_list[i].name[0] ? extrasClampGrams(list[i].grams) : 0.0f;
    keyFor(key, sizeof(key), "xtr_n", i);
    ok &= prefsPutString(key, s_list[i].name);
    keyFor(key, sizeof(key), "xtr_g", i);
    ok &= prefsPutFloat(key, s_list[i].grams);
  }
  s_default = extrasClampMask(s_list, default_mask);
  s_cached_id = -1;
  ok &= prefsPutUChar("xtr_def", s_default);
  return ok;
}

uint8_t spoolExtrasDefault() {
  return s_default;
}

uint8_t spoolExtrasFor(int spool_id) {
  if (spool_id <= 0) return extrasResolve(s_list, SPOOL_EXTRAS_FOLLOW_DEFAULT, s_default);
  if (spool_id == s_cached_id) return s_cached_mask;
  char key[16];
  keyFor(key, sizeof(key), "xs", spool_id);
  const uint8_t own = prefsGetUChar(key, SPOOL_EXTRAS_FOLLOW_DEFAULT);
  s_cached_id = spool_id;
  s_cached_mask = extrasResolve(s_list, own, s_default);
  return s_cached_mask;
}

bool spoolExtrasSetFor(int spool_id, uint8_t mask) {
  if (spool_id <= 0) return false;
  const uint8_t store = extrasToStore(s_list, mask, s_default);
  s_cached_id = -1;
  char key[16];
  keyFor(key, sizeof(key), "xs", spool_id);
  // Nothing is written for a spool that never left the default.
  if (store == SPOOL_EXTRAS_FOLLOW_DEFAULT && !prefsHasKey(key)) return true;
  return prefsPutUChar(key, store);
}

float spoolExtrasGrams(uint8_t mask) {
  return extrasMaskGrams(s_list, mask);
}

void spoolExtrasNames(uint8_t mask, char* out, size_t n) {
  extrasMaskNames(s_list, mask, out, n);
}

float spoolExtrasNow() {
  return spoolExtrasGrams(spoolExtrasFor(sm_found ? sm_id : 0));
}

float spoolExtrasBareReading() {
  return extrasBare(scale_weight_g, spoolExtrasNow());
}
