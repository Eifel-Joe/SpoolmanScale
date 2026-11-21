#pragma once

#include <stdint.h>

#include "boards/board.h"

// ============================================================
//  SAVING POWER ON THE 7B
//
//  Spec 5.3 AD (V81, V82). Before the deep sleep the 7B switches off more
//  than the backlight: the NFC reader, the weighing and the SD card, and it
//  holds the RS485 transmit line high. The display's converter and its reset
//  stay on (V82). On the battery the reader is on only while something lies
//  on the scale. A wake by the touch line without a touch sleeps on before
//  anything else runs. The rules are in services/sleep_power_rules.h.
//
//  Only the 7B sets BOARD_SLEEP_CUTS; everywhere else these are empty
//  functions and the reader is never parked.
// ============================================================

#ifndef BOARD_SLEEP_CUTS
#define BOARD_SLEEP_CUTS 0
#endif

// The very first call of appSetup(), before the serial port and before
// boardEarlyInit() puts the touch chip in reset. After a wake by the touch
// line with nothing reported by the chip it goes straight back to sleep
// (sleeppwr::wakeRule()). Otherwise it lets go of the lines held through the
// sleep (reader reset, RS485), on every start: after this firmware's sleep,
// after another firmware's, after a crash.
void sleepPowerEarly();

// Once the logger is up: how many wakes without a touch slept on since the
// last start.
void sleepPowerReport();

// From panelPrepareDeepSleep() of the 7B, right before it arms the touch
// line: the level that wakes, kept through the sleep for the wake rule.
void sleepPowerArmed(uint8_t wake_level);

// After displayPrepareDeepSleep(), right before esp_deep_sleep_start(): the
// shutdown list (sleeppwr::sleepCuts()). Writes its log line itself and
// deselects the SD card last, so nothing may log after it.
void sleepPowerBeforeSleep();

// Every pass of the main loop: the reader rule on the battery
// (sleeppwr::readerGate()). waiting_for_tag: a screen or a write waits for a
// tag on the reader.
void sleepPowerReaderTick(float weight_g, bool waiting_for_tag);

// The reader is switched off by the rule: the main loop does not poll or
// revive it, the diagnosis does not call it missing.
bool sleepPowerReaderParked();

// Switches a parked reader on now, for the reset probe. The rule may park it
// again ten seconds later.
void sleepPowerReaderWake();

// Something outside the main loop waits for a tag (a scan request, the tag
// page in the browser): keeps the reader on for a while.
void sleepPowerReaderWanted();
