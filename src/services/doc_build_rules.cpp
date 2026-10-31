#include "services/doc_build_rules.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool inside(int x, int y) {
  return x >= 0 && y >= 0 && x < DOC_UI_W && y < DOC_UI_H;
}

bool docTouchMake(const DocTouchAsk& ask, DocTouch& out) {
  if (!inside(ask.x, ask.y)) return false;
  const bool swipe = !(ask.x2 == -1 && ask.y2 == -1);
  if (swipe && !inside(ask.x2, ask.y2)) return false;
  const int ms = ask.ms == 0 ? DOC_TOUCH_TAP_MS : ask.ms;
  if (ms < DOC_TOUCH_MIN_MS || ms > DOC_TOUCH_MAX_MS) return false;
  out.x  = (int16_t)ask.x;
  out.y  = (int16_t)ask.y;
  out.x2 = (int16_t)(swipe ? ask.x2 : ask.x);
  out.y2 = (int16_t)(swipe ? ask.y2 : ask.y);
  out.ms = (uint16_t)ms;
  return true;
}

// start + d * e / ms, rounded half away from zero, in whole numbers.
static int16_t step(int16_t from, int16_t to, uint32_t e, uint16_t ms) {
  const long d = (long)to - from;
  const long num = d * (long)e * 2;
  const long den = 2L * ms;
  const long q = num >= 0 ? (num + ms) / den : -((-num + ms) / den);
  return (int16_t)(from + q);
}

bool docTouchAt(const DocTouch& t, uint32_t elapsed_ms, int16_t& x, int16_t& y) {
  if (elapsed_ms >= t.ms) return false;
  x = step(t.x, t.x2, elapsed_ms, t.ms);
  y = step(t.y, t.y2, elapsed_ms, t.ms);
  return true;
}

int32_t docSimRaw(float grams, int32_t zero_offset, float cal_factor) {
  // NaN fails both comparisons and lands here too.
  if (!(cal_factor > 0.0f || cal_factor < 0.0f)) return zero_offset;
  return zero_offset + (int32_t)lroundf(grams * cal_factor);
}

bool docSimParse(const char* arg, float& grams, bool& off) {
  if (!arg || !*arg) return false;
  if (strcasecmp(arg, "off") == 0) {
    off = true;
    return true;
  }
  char* end = nullptr;
  const float g = strtof(arg, &end);
  if (end == arg || *end != '\0') return false;
  // NaN fails the range check.
  if (!(g >= -100.0f && g <= 10000.0f)) return false;
  grams = g;
  off = false;
  return true;
}

bool docWriteAllowed(const char* method) {
  if (!method) return false;
  return strcasecmp(method, "GET") == 0 || strcasecmp(method, "HEAD") == 0;
}
