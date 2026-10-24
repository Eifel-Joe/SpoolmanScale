#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
//  WHAT HANGS ON A SPOOL BESIDES ITS FILAMENT
//
//  A filament clip, an RFID tag holder: a few grams each that are neither
//  filament nor the empty spool the backend knows. The user names them
//  once in the web settings, eight places with a name and a weight, and
//  ticks up to three as the default. A spool that carries something else
//  gets its own choice on the weight popup, remembered for that spool.
//
//  Whatever is chosen comes off every reading that becomes a filament
//  weight: the net on the main screen, the weight popup's buttons, auto
//  weighing, the AMS note and a weighed empty spool. The gross a backend
//  is told is the reading without it, as the bag's is: what the scale
//  would have shown for the spool alone.
// ============================================================

// The places, the limits and what a mask means are in the rules unit: it holds
// no Arduino and no NVS, so a host test can check it. This part keeps the list
// and the choices in NVS and asks the rules for everything else.
#include "services/spool_extras_rules.h"

// From NVS, once at boot.
void spoolExtrasLoad();

// The eight places, used or not. A place keeps its index when another is
// cleared, so a spool's choice still means the same parts.
const SpoolExtra& spoolExtraAt(int i);
bool spoolExtraUsed(int i);
bool spoolExtrasAny();            // at least one place has a part

// Replaces the list and the default. Names are trimmed, weights clamped,
// the default limited to SPOOL_EXTRAS_PICK used places.
bool spoolExtrasSave(const SpoolExtra* list, uint8_t default_mask);
uint8_t spoolExtrasDefault();

// A spool's choice: its own if one was set, else the default.
uint8_t spoolExtrasFor(int spool_id);
// Remembered for the spool; the default's mask forgets the spool's own.
bool spoolExtrasSetFor(int spool_id, uint8_t mask);

// Bit i set: place i. Unused places and picks past SPOOL_EXTRAS_PICK drop.
uint8_t spoolExtrasClamp(uint8_t mask);
float spoolExtrasGrams(uint8_t mask);
// "Clip 1 + RFID 1", empty for none.
void spoolExtrasNames(uint8_t mask, char* out, size_t n);

// What comes off the reading now: the choice for the spool on the pad
// (sm_id), the default when none is known yet.
float spoolExtrasNow();

// The reading without the parts: what the scale would show for the spool
// alone. Never below zero.
float spoolExtrasBareReading();
