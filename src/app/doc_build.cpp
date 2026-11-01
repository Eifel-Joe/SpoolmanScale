#include "app/doc_build.h"

#if defined(SSC_ACCEPTANCE)

#include <Arduino.h>
#include <string.h>

#include "hardware/sd_logger.h"

// Everything below lives in RAM only. A restart ends a touch and a simulated
// weight, which is what an acceptance wants of a scale that is used normally
// afterwards: nothing here goes to flash or preferences, the log lines aside.

static DocTouch s_touch;
static bool     s_touch_on   = false;
static uint32_t s_touch_from = 0;

static bool     s_sim_on   = false;
static float    s_sim_g    = 0.0f;
static uint32_t s_sim_from = 0;

void docTouchStart(const DocTouch& t) {
  s_touch      = t;
  s_touch_from = millis();
  s_touch_on   = true;
}

bool docTouchPoint(int16_t& x, int16_t& y) {
  if (!s_touch_on) return false;
  if (docTouchAt(s_touch, millis() - s_touch_from, x, y)) return true;
  s_touch_on = false;
  return false;
}

void docTouchCancel() {
  if (!s_touch_on) return;
  s_touch_on = false;
  logSD("Acceptance build: web touch ended by a finger");
}

// Every new value starts the 15 minutes again: what ends a simulated weight is
// that nobody has set one for that long, not the age of the first.
void docSimSet(float grams) {
  s_sim_g    = grams;
  s_sim_from = millis();
  s_sim_on   = true;
}

void docSimOff() {
  s_sim_on = false;
}

bool docSimActive() {
  return s_sim_on;
}

float docSimGrams() {
  return s_sim_g;
}

// Why it ends by itself: the scale is used normally after an acceptance, and a
// simulated weight that was forgotten would be taken for a real one - a real
// spool would get a weight that was never on the pad. The main loop calls this
// before it reads the scale, so a weight that ran out is gone in the same pass.
void docSimTick(uint32_t now_ms) {
  if (!s_sim_on) return;
  if (!docSimExpired(s_sim_from, now_ms)) return;
  s_sim_on = false;
  logSD("Acceptance build: simulated weight off after 15 min");
}

// The release literal is cut out of FW_VERSION here instead of being written a
// second time: it lives in app_config.h alone, the one place the release run
// and scripts/check.sh read, and a copy would drift at the next version.
// FW_VERSION is that literal followed by SSC_VERSION_SUFFIX ("-doku" in this
// firmware), so the literal is the first sizeof(FW_VERSION) -
// sizeof(SSC_VERSION_SUFFIX) characters, each sizeof counting its terminator.
// The buffer is static and zeroed, so the copy leaves the end terminated; the
// acceptance firmware keeps its "-abnahme" on purpose.
const char* fwVersionShown() {
#if defined(SSC_DOC_BUILD)
  static char shown[sizeof(FW_VERSION) - sizeof(SSC_VERSION_SUFFIX) + 1];
  if (shown[0] == '\0') memcpy(shown, FW_VERSION, sizeof(shown) - 1);
  return shown;
#else
  return FW_VERSION;
#endif
}

#endif  // SSC_ACCEPTANCE
