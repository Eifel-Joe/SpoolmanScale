#pragma once

#include <stddef.h>
#include <stdint.h>

// SSC_ACCEPTANCE comes from here when the documentation firmware sets only
// SSC_DOC_BUILD: without this include the #if below would be false and that
// firmware would silently lack the remote control.
#include "app_config.h"
#include "services/doc_build_rules.h"

// The state of the acceptance and documentation builds (SSC_ACCEPTANCE, never
// in a release; services/doc_build_rules.h): the one touch from the web that is
// running, and the simulated weight. Everything here runs in the main loop -
// the web server is served from appLoop(), and LVGL reads its touch there too -
// so nothing needs a lock.
//
// A release gets the stubs of the #else branch: they do nothing and say "no
// touch, no simulated weight", so display.cpp, scale.cpp and the main loop call
// this without a switch of their own and compile to the code they had before.
// The same goes for the version a screen shows (fwVersionShown() at the end):
// the screens call it in every build, and only the documentation firmware
// answers with something else than FW_VERSION.
#if defined(SSC_ACCEPTANCE)

// Starts t now; a touch still running is replaced.
void docTouchStart(const DocTouch& t);
// The point of the running touch, false when none runs (any more).
bool docTouchPoint(int16_t& x, int16_t& y);
// A real finger came: the web touch ends at once.
void docTouchCancel();

// A simulated weight from now on; it ends by itself after DOC_SIM_MAX_MS. The
// auto-tare holds off while it runs (app_loop.cpp), but a tare or calibration
// that a person starts is not guarded: it would store the simulated reading as
// the zero or the factor. Set it off first.
void  docSimSet(float grams);
void  docSimOff();
bool  docSimActive();
float docSimGrams();
// Called from the main loop with millis(): ends a simulated weight that ran out.
void  docSimTick(uint32_t now_ms);

// A made-up answer of the scale's own server (Spec 5.3 AA, v0.8.7-beta.2;
// services/sim_answer_rules.h): the next `count` requests whose path holds
// `word` get `code` without being sent, a 429 with `retry` as its Retry-After.
// Unlike the rest of this file it is asked from both cores - a backend job
// sends from its own task - so it keeps its state under a lock.
void docAnswerSet(int code, int count, const char* word, const char* retry);
void docAnswerOff();
// For BackendHttp, before it sends to its server: true and the answer when
// this request is one of them; the retry text may come back empty.
bool docAnswerTake(const char* uri, int* code, char* retry, size_t retry_len);

// A made-up, empty NTAG215 on the reader (services/sim_tag_rules.h): the door
// of the reader (hardware/nfc.cpp) answers with it while it is set.
void docTagSet(const uint8_t uid[7]);
void docTagOff();
bool docTagActive();
bool docTagUid(uint8_t* uid, uint8_t* uid_len);
bool docTagPage(uint8_t page, uint8_t* data);

// A bay the recheck of an assignment sees as occupied or empty (Spec 5.3 AA,
// v0.8.7-beta.4, V71): the AMS of Jörg's printers cannot be made to report a
// spool in an empty bay without a spool. One bay at a time; read in the loop.
void docBaySet(int printer_id, int ams_id, int tray_id, bool present);
void docBayOff();
// 1 occupied, 0 empty for the bay that is set, -1 for any other (ask the printer).
int  docBayPresence(int printer_id, int ams_id, int tray_id);
// A made-up answer of the Bluetooth label printer (Spec 5.3 AA, v0.8.7-beta.6,
// V74): nobody here has one of these printers. The next print on the chosen
// Bluetooth printer ends without the radio as taken (1) or not reached (0),
// after it was drawn and checked as for a real one; the next scan finds the
// printer set up (1) or not (0), after it ran as a real one. Each is taken
// once; -1 clears it. Never asked for the Brother. Read in the loop.
void docPrinterPrintSet(int accepted);
void docPrinterScanSet(int found);
// The answer that is set, once: 1 or 0, -1 for none (the radio decides).
int docPrinterPrintTake();
int docPrinterScanTake();

// The version every text on the display shows (header line, info screen, the
// firmware screens): FW_VERSION in the acceptance firmware, which says
// "A" there (Spec V61), and FW_VERSION without "-doku" in the documentation
// firmware, because a screenshot of the documentation shows the release one
// can download. The web pages, the log, status.json and the update check keep
// FW_VERSION: they name the build that runs.
const char* fwVersionShown();

#else

static inline void  docTouchStart(const DocTouch&) {}
static inline bool  docTouchPoint(int16_t&, int16_t&) { return false; }
static inline void  docTouchCancel() {}
static inline void  docSimSet(float) {}
static inline void  docSimOff() {}
static inline bool  docSimActive() { return false; }
static inline float docSimGrams() { return 0.0f; }
static inline void  docSimTick(uint32_t) {}
static inline void  docAnswerSet(int, int, const char*, const char*) {}
static inline void  docAnswerOff() {}
static inline bool  docAnswerTake(const char*, int*, char*, size_t) { return false; }
static inline void  docTagSet(const uint8_t*) {}
static inline void  docTagOff() {}
static inline bool  docTagActive() { return false; }
static inline bool  docTagUid(uint8_t*, uint8_t*) { return false; }
static inline bool  docTagPage(uint8_t, uint8_t*) { return false; }
static inline void  docBaySet(int, int, int, bool) {}
static inline void  docBayOff() {}
static inline int   docBayPresence(int, int, int) { return -1; }
static inline void  docPrinterPrintSet(int) {}
static inline void  docPrinterScanSet(int) {}
static inline int docPrinterPrintTake() { return -1; }
static inline int docPrinterScanTake() { return -1; }

// A release shows what it is.
static inline const char* fwVersionShown() { return FW_VERSION; }

#endif
