#pragma once

#include <stdint.h>

#include "app_config.h"
#include "services/battery_rules.h"

// ============================================================
//  THE BATTERY OF THE 7B (Spec 5.3 AB, V60)
//
//  Reads the board's battery input four times a second from the main loop,
//  hands every ten seconds to the rules (battery_rules.h) and acts on what
//  they say: a log line on every change of supply and step, the warnings,
//  the header line. On a board without BOARD_BATTERY_SENSE every function
//  here is a no-op and the supply stays UNKNOWN: no display, no warning,
//  no lock.
// ============================================================

// From the main loop, every pass.
void batteryTick(uint32_t now_ms);

// What the rules know now, for the header and status.json.
const bat::State& batteryState();
// "unknown", "none", "battery" or "charging": the word in status.json and
// the log.
const char* batterySupplyName();
// The last window of ten seconds as it was read: its mean in millivolts and
// the span of its readings in counts. Diagnosis only (status.json): the
// rules decide on these, so a support question about the display starts here.
void batteryLastWindow(uint32_t& mv, uint16_t& span);

// The update lock of Spec 5.3 T (V60): true on the battery below 50 % and
// below the warning voltage. Every way that writes a firmware asks it before
// the first byte.
bool batteryUpdateBlocked();

#if defined(SSC_ACCEPTANCE)
// The acceptance firmware's simulated battery (Spec 5.3 AA, V60): readings
// made up around mv with the spread the board showed for that supply, fed
// through the same rules instead of the chip's. BAT_SIM_OFF reads the chip
// again.
enum BatterySim : uint8_t { BAT_SIM_OFF = 0, BAT_SIM_NONE, BAT_SIM_BATTERY, BAT_SIM_CHARGING };
void batterySimSet(BatterySim mode, uint32_t mv);
#endif
