#pragma once

#include <stdint.h>

#include "app_config.h"

// Diagnosis of the 7B light (n61): never in a release, reverted once the cause
// is found. In the 7B's acceptance firmware every frame to the CH32V003 is
// kept with its time, and once the frames of a light command are over, the
// registers are read back and the lot goes into the log ("LD ..."). The
// events of the display power ("dim", "off", "wake from ...") mark what the
// frames were for. Everywhere else these are empty stubs.
#if defined(SSC_ACCEPTANCE) && defined(BOARD_WAVESHARE_S3_LCD7B)
#define LIGHT_DIAG 1

// What the display power did; one per batch of frames, the last one wins.
void lightDiagEvent(const char* what);
// From the main loop: reads back and logs a batch once its frames are over.
void lightDiagTick();

#include <WebServer.h>
// GET  /api/diag/light            the registers now, against what was sent
// POST /api/diag/light?age=<ms>   the idle timer that much older, so the main
//                                 loop dims or switches off on its next pass
// POST /api/diag/light/seq?...    a test sequence of frames, see light_diag.cpp
void lightDiagRoutes(WebServer& srv);

#else

static inline void lightDiagEvent(const char*) {}
static inline void lightDiagTick() {}

#endif
