#pragma once

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
#if defined(SSC_ACCEPTANCE)

// Starts t now; a touch still running is replaced.
void docTouchStart(const DocTouch& t);
// The point of the running touch, false when none runs (any more).
bool docTouchPoint(int16_t& x, int16_t& y);
// A real finger came: the web touch ends at once.
void docTouchCancel();

// A simulated weight from now on; it ends by itself after DOC_SIM_MAX_MS.
void  docSimSet(float grams);
void  docSimOff();
bool  docSimActive();
float docSimGrams();
// Called from the main loop with millis(): ends a simulated weight that ran out.
void  docSimTick(uint32_t now_ms);

#else

static inline void  docTouchStart(const DocTouch&) {}
static inline bool  docTouchPoint(int16_t&, int16_t&) { return false; }
static inline void  docTouchCancel() {}
static inline void  docSimSet(float) {}
static inline void  docSimOff() {}
static inline bool  docSimActive() { return false; }
static inline float docSimGrams() { return 0.0f; }
static inline void  docSimTick(uint32_t) {}

#endif
