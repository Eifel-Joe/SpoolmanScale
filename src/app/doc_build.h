#pragma once

#include <stdint.h>

#include "services/doc_build_rules.h"

// The state of the documentation build (services/doc_build_rules.h): the one
// touch from the web that is running, and the simulated weight. Everything
// here runs in the main loop - the web server is served from appLoop(), and
// LVGL reads its touch there too - so nothing needs a lock.

// Starts t now; a touch still running is replaced.
void docTouchStart(const DocTouch& t);
// The point of the running touch, false when none runs (any more).
bool docTouchPoint(int16_t& x, int16_t& y);
// A real finger came: the web touch ends at once.
void docTouchCancel();

void  docSimSet(float grams);
void  docSimOff();
bool  docSimActive();
float docSimGrams();
