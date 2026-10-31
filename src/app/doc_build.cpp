#include "app/doc_build.h"

#include <Arduino.h>

#include "hardware/sd_logger.h"

static DocTouch s_touch;
static bool     s_touch_on   = false;
static uint32_t s_touch_from = 0;

static bool  s_sim_on = false;
static float s_sim_g  = 0.0f;

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
  logSD("Doc build: web touch ended by a finger");
}

void docSimSet(float grams) {
  s_sim_g  = grams;
  s_sim_on = true;
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
