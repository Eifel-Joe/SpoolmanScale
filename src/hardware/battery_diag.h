#pragma once

#include <stdint.h>

#include "app_config.h"

// Measurement of the 7B battery input (w05, beta.20): never in a release,
// reverted once the thresholds are measured. In the 7B's acceptance firmware
// the main loop reads the CH32V003's ADC register 0x06 (VBAT through the
// 20 k / 10 k divider) four times a second, keeps the last minute and logs a
// summary ("BD ..."). Everywhere else an empty stub.
#if defined(SSC_ACCEPTANCE) && defined(BOARD_WAVESHARE_S3_LCD7B)
#define BATTERY_DIAG 1

// From the main loop: one reading every 250 ms, a log line every log period.
void batteryDiagTick();

#include <WebServer.h>
// GET  /api/diag/battery                    the last minute, see battery_diag.cpp
// POST /api/diag/battery?log=<s>            log period, 0 for none
// POST /api/diag/battery/burst?n=&gap=&mode= a burst of readings, rs or stop
void batteryDiagRoutes(WebServer& srv);

#else

static inline void batteryDiagTick() {}

#endif
