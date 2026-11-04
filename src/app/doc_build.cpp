#include "app/doc_build.h"

#if defined(SSC_ACCEPTANCE)

#include <Arduino.h>
#include <string.h>

#include "hardware/sd_logger.h"
#include "services/sim_answer_rules.h"
#include "services/sim_tag_rules.h"

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
// before it reads the scale, so the raw read of that pass is already the real
// one (scaleHardwareReadRaw() asks docSimActive()). The weight the display shows
// and the web sends is not the raw read but the moving average over
// SCALE_FILTER_SIZE (8) readings, one every 200 ms, so simulated samples stay in
// it for about 1.6 s after the end, whether it ran out here or docSimOff() ended
// it, and the shown and sent weight follows within that time. Auto-tare cannot
// fire on those leftovers: it needs 3 s of steady weight (AUTO_TARE_STABLE_MS),
// its timer is held at zero while the simulation is on (app_loop.cpp), and the
// leftovers are out of the window after 1.6 s.
void docSimTick(uint32_t now_ms) {
  if (!s_sim_on) return;
  if (!docSimExpired(s_sim_from, now_ms)) return;
  s_sim_on = false;
  logSD("Acceptance build: simulated weight off after 15 min");
}

// ---- a made-up answer of the server (v0.8.7-beta.2) -----------------------
// The one piece of this file a backend job reaches from its own task, so it
// sits under a lock; set from the web route in the loop. The route checked the
// arguments (simAnswerArgsOk()).
static portMUX_TYPE s_answer_mux = portMUX_INITIALIZER_UNLOCKED;
static SimAnswer    s_answer;

void docAnswerSet(int code, int count, const char* word, const char* retry) {
  portENTER_CRITICAL(&s_answer_mux);
  simAnswerSet(s_answer, code, count, word, retry);
  portEXIT_CRITICAL(&s_answer_mux);
}

void docAnswerOff() {
  portENTER_CRITICAL(&s_answer_mux);
  simAnswerClear(s_answer);
  portEXIT_CRITICAL(&s_answer_mux);
}

bool docAnswerTake(const char* uri, int* code, char* retry, size_t retry_len) {
  bool hit = false;
  int  got = 0;
  char wait[sizeof(s_answer.retry)] = "";
  portENTER_CRITICAL(&s_answer_mux);
  if (simAnswerTake(s_answer, uri)) {
    hit = true;
    got = s_answer.code;
    memcpy(wait, s_answer.retry, sizeof(wait));
  }
  portEXIT_CRITICAL(&s_answer_mux);
  if (!hit) return false;
  if (code) *code = got;
  if (retry && retry_len) {
    strncpy(retry, wait, retry_len - 1);
    retry[retry_len - 1] = '\0';
  }
  return true;
}

// ---- a made-up tag on the reader (v0.8.7-beta.2) ---------------------------
// Read by the reader's door in the loop, like the touch; no lock.
static bool    s_tag_on = false;
static uint8_t s_tag_uid[SIM_TAG_UID_LEN];

void docTagSet(const uint8_t uid[7]) {
  memcpy(s_tag_uid, uid, SIM_TAG_UID_LEN);
  s_tag_on = true;
}

void docTagOff() {
  s_tag_on = false;
}

bool docTagActive() {
  return s_tag_on;
}

bool docTagUid(uint8_t* uid, uint8_t* uid_len) {
  if (!s_tag_on) return false;
  memcpy(uid, s_tag_uid, SIM_TAG_UID_LEN);
  *uid_len = SIM_TAG_UID_LEN;
  return true;
}

// True with the page while the tag lies there and the page exists; false
// otherwise, also past its last page, as the reader says for a missing page.
bool docTagPage(uint8_t page, uint8_t* data) {
  if (!s_tag_on) return false;
  return simNtagPage(s_tag_uid, page, data);
}

// The release literal is cut out of FW_VERSION here instead of being written a
// second time: it lives in app_config.h alone, the one place the release run
// and scripts/check.sh read, and a copy would drift at the next version.
// FW_VERSION is that literal followed by SSC_VERSION_SUFFIX ("-doku" in this
// firmware), so the literal is the first sizeof(FW_VERSION) -
// sizeof(SSC_VERSION_SUFFIX) characters, each sizeof counting its terminator.
// The buffer is static and zeroed, so the copy leaves the end terminated; the
// acceptance firmware keeps its "A" on purpose.
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
