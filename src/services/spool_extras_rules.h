#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
//  WHAT HANGS ON A SPOOL BESIDES ITS FILAMENT: THE RULES
//
//  A filament clip, an RFID tag holder: a few grams each that are neither
//  filament nor the empty spool the backend knows. The user names them
//  once in the web settings, eight places with a name and a weight, and
//  ticks up to three as the default. A spool that carries something else
//  gets its own choice on the weight popup, remembered for that spool.
//
//  Whatever is chosen comes off every reading that becomes a filament
//  weight. The gross a backend is told is the reading without it: what
//  the scale would have shown for the spool alone.
//
//  The rules live here, without Arduino, NVS or the screens: a host test
//  holds them to the values in test_spool_extras_rules.py, and the part
//  that keeps the list and the choices in NVS (spool_extras.cpp) asks this
//  unit instead of keeping rules of its own. The list is always a
//  parameter, eight entries, so that the test can hand in its own.
//
//  A choice is a mask: bit i set means place i. Two rules shape every mask:
//
//    used     A place counts when it has a name. A place keeps its index
//             when another is cleared, so a choice made earlier still
//             means the same parts; a choice that names a place cleared
//             since comes to nothing.
//
//    picked   At most SPOOL_EXTRAS_PICK places on one spool. Of a mask
//             with more, the used ones with the lowest index stay and
//             the rest drop.
//
//  What is stored for a spool is a mask or SPOOL_EXTRAS_FOLLOW_DEFAULT.
//  0xFF can be that marker because no real mask has eight bits set (three
//  at most), and it is needed because NVS here has no erase: a spool put
//  back on the default must follow the default when that changes later, not
//  keep the mask that happened to be the default on the day it was put back.
//  So a choice equal to the default is stored as 0xFF, and 0xFF is read
//  back as whatever the default is now. Nothing at all stored for a spool
//  also follows the default, and not writing anything for a spool that never
//  left it is the storage's job, not a rule of this unit.
// ============================================================

#define SPOOL_EXTRAS_MAX             8      // places in the settings
#define SPOOL_EXTRAS_PICK            3      // at most this many on one spool
#define SPOOL_EXTRA_NAME_LEN        20      // 19 characters and the terminator
#define SPOOL_EXTRA_MAX_G       200.0f      // one part; a clip weighs a few grams
#define SPOOL_EXTRAS_FOLLOW_DEFAULT 0xFF    // stored for a spool: the default

struct SpoolExtra {
  char  name[SPOOL_EXTRA_NAME_LEN];   // empty: the place is unused
  float grams;
};

// True if place i (0 .. SPOOL_EXTRAS_MAX - 1) has a name. An index outside
// the eight, or no list, is no place.
bool extrasPlaceUsed(const SpoolExtra* list, int i);

// The mask cut to what can be on a spool: unused places drop, and of the
// rest only the first SPOOL_EXTRAS_PICK stay.
uint8_t extrasClampMask(const SpoolExtra* list, uint8_t mask);

// The weights of the places in the mask, cut as extrasClampMask cuts it, so
// that a place that is not used weighs nothing even if it holds a weight.
float extrasMaskGrams(const SpoolExtra* list, uint8_t mask);

// "Clip + RFID", the names of the places in the mask (cut as above), empty
// for none. Writes at most n bytes including the terminator, cut short if
// they do not fit, and nothing if out is null or n is 0.
void extrasMaskNames(const SpoolExtra* list, uint8_t mask, char* out, size_t n);

// A weight is 0 .. SPOOL_EXTRA_MAX_G: not a number, infinite and negative
// are 0, more than the limit is the limit.
float extrasClampGrams(float g);

// src without the blanks on both sides, cut to n - 1 characters. Writes the
// terminator even for an empty or null src; src may be dst. Nothing if dst is
// null or n is 0.
void extrasTrimName(char* dst, size_t n, const char* src);

// What a spool takes off, from what is stored for it: the default (cut) for
// SPOOL_EXTRAS_FOLLOW_DEFAULT, else the stored choice cut as extrasClampMask
// cuts it. A choice of 0 stays no parts, and a choice that names only cleared
// places comes to nothing; neither falls back to the default.
uint8_t extrasResolve(const SpoolExtra* list, uint8_t stored, uint8_t default_mask);

// What to store for a choice: SPOOL_EXTRAS_FOLLOW_DEFAULT if it equals the
// default (both cut), else the choice cut. So no parts with no default is
// stored as "follows" too, and no parts with a default stays 0.
uint8_t extrasToStore(const SpoolExtra* list, uint8_t pick, uint8_t default_mask);

// The reading without the parts: what the scale would show for the spool
// alone. Never below zero, and a result that is not a number is zero.
float extrasBare(float reading, float parts_g);
