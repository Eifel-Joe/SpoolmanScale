#pragma once

#include <stdint.h>

// ============================================================
//  THE DOCUMENTATION BUILD: TOUCH FROM THE WEB, SIMULATED WEIGHT, READING ONLY
//
//  This firmware exists for the screenshots of the documentation only and is
//  never released (Joerg, 31.10.2026). It lets the web interface touch the
//  display, so the screens can be reached without a finger on the glass, and
//  put a simulated weight on the pad, so a screenshot shows a real-looking
//  reading. To keep a simulated weight or a wrong tap away from real data, it
//  only reads from servers: every writing request is refused.
//
//  The rules live here without Arduino or LVGL, so test_doc_build_rules.py
//  holds them; app/doc_build.cpp keeps the state, web/web_doc.cpp the routes.
// ============================================================

#define DOC_UI_W           480    // the interface, as on the screenshots
#define DOC_UI_H           320
#define DOC_TOUCH_TAP_MS   80     // a tap when no duration is given
#define DOC_TOUCH_MIN_MS   20
#define DOC_TOUCH_MAX_MS   3000

// A touch from the web: down at (x, y), moving to (x2, y2) over ms, lifted
// after ms. A tap or a long press has x2 == x and y2 == y.
struct DocTouch {
  int16_t x, y, x2, y2;
  uint16_t ms;
};

// A touch as the web request asks for it: ms 0 is a tap (DOC_TOUCH_TAP_MS),
// x2 and y2 both -1 mean no swipe.
struct DocTouchAsk {
  int x, y, ms, x2, y2;
};

// Checks a request and fills out. Every point must lie inside the interface,
// DOC_UI_W x DOC_UI_H. False, out untouched, for anything else.
bool docTouchMake(const DocTouchAsk& ask, DocTouch& out);

// Where the finger is elapsed_ms after it went down: true and the point
// (rounded to the nearest pixel) while it is down, false once ms has passed.
bool docTouchAt(const DocTouch& t, uint32_t elapsed_ms, int16_t& x, int16_t& y);

// The raw reading that the loop turns back into grams, (raw - zero_offset) /
// cal_factor. A scale without a usable factor gets its zero reading.
int32_t docSimRaw(float grams, int32_t zero_offset, float cal_factor);

// The argument of the weight request: "off" (any case) ends the simulation,
// a number from -100 to 10000 grams sets it. False for anything else.
bool docSimParse(const char* arg, float& grams, bool& off);

// Whether this build may send a request with the method: GET and HEAD only.
bool docWriteAllowed(const char* method);
