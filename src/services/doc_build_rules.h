#pragma once

#include <stdint.h>

// ============================================================
//  THE ACCEPTANCE AND DOCUMENTATION BUILDS: TOUCH FROM THE WEB, SIMULATED
//  WEIGHT, AND (DOCUMENTATION ONLY) READING FROM SERVERS ONLY
//
//  Two firmwares that are never released share this. The acceptance firmware
//  (SSC_ACCEPTANCE) is built to accept a beta on the scale itself, the
//  documentation firmware (SSC_DOC_BUILD) to take the screenshots of the
//  documentation. Both let the web interface touch the display, so the screens
//  can be reached without a finger on the glass, and put a simulated weight on
//  the pad, so a screen shows a real-looking reading. The documentation
//  firmware goes one step further: to keep a simulated weight or a wrong tap
//  away from real data it only reads from servers and refuses every writing
//  request (docRequestAllowed), sends no label to a printer
//  (docLabelNotPrinted in label_printer.cpp) and writes no tag
//  (docTagWriteRefused in hardware/nfc.cpp). The acceptance firmware must
//  write - it sends weights, creates and deletes test spools - so it has no
//  such lock.
//
//  A simulated weight is meant for a test spool. The scale is used normally
//  after an acceptance, so one that is forgotten must never reach a real
//  spool: it ends by itself after DOC_SIM_MAX_MS, and a restart ends it (it
//  lives in RAM only).
//
//  The rules live here without Arduino, LVGL or a switch, so
//  test_doc_build_rules.py always holds them; app/doc_build.cpp keeps the
//  state, web/web_doc.cpp the routes.
// ============================================================

#define DOC_UI_W           480    // the interface, as on the screenshots
#define DOC_UI_H           320
#define DOC_TOUCH_TAP_MS   80     // a tap when no duration is given
#define DOC_TOUCH_MIN_MS   20
#define DOC_TOUCH_MAX_MS   3000
#define DOC_SIM_MAX_MS     900000   // 15 minutes: how long a simulated weight stays

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

// Whether a simulated weight set at set_ms has run out at now_ms: DOC_SIM_MAX_MS
// or more have passed. Both are millis() values; the difference is taken in
// unsigned arithmetic, so the wrap of millis() after 49 days changes nothing.
bool docSimExpired(uint32_t set_ms, uint32_t now_ms);

// Moonraker's query of the printer objects: a POST with the objects asked for
// in its body, that reads them and changes nothing.
#define DOC_MOONRAKER_QUERY "/printer/objects/query"

// Whether the documentation build may send a request with the method to the
// path (the request's path and query, as HTTPClient keeps it): GET and HEAD
// anywhere, POST to exactly DOC_MOONRAKER_QUERY, nothing else. (The acceptance
// firmware does not ask; it has to write.)
bool docRequestAllowed(const char* method, const char* path);
