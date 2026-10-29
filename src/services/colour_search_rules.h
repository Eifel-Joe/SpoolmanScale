#pragma once

#include <stddef.h>

// ============================================================
//  THE SEARCH IN A COLOUR LIST: THE RULE (V40)
//
//  "Enter filament" (its colour step) and "New from database" (its colour
//  families and entries) cut the list they have loaded to the colours whose
//  name holds a word typed on the keyboard. Nothing is loaded for it: the
//  database list stays the one of the maker and the material chosen
//  (SpoolmanDB files Sunlu's "2.0" colours under PLA+, and a PLA search does
//  not reach them), the entry's list the one its sources built for the line.
//
//  The rule lives here without Arduino or LVGL, so a host test holds it to
//  the values in test_colour_search_rules.py; ui/colour_search.cpp is what
//  both screens share on the panel. Letters are compared in any case through
//  the fold of services/text_fold.h, the keyboard's umlauts and accents
//  included (V43).
// ============================================================

// The word, terminator included: 23 bytes, cut at a character boundary.
#define COLOUR_SEARCH_MAX  24

// True when name holds word anywhere: "2.0 Bone White" holds "bone", "BONE"
// and "e w", not "bonewhite". Letters are compared in any case, the capital
// umlauts and accents of Latin-1 included: a capital U umlaut typed finds the
// name with the small one, and the other way round. One character at a time
// through textFoldNext(); nothing is spelled out or stripped, so the letter u
// is not the letter u umlaut and "ue" does not find it.
// Wurzel: the names are mostly English, but the keyboard has umlauts and
//   accents now (QWERTZ, AZERTY), so a word typed can start with a capital
//   umlaut where the name has the small one. The fold of A-Z alone, with
//   every other byte as it is, made the two different letters (V43).
// Fix: both sides are read a character at a time through textFoldNext()
//   (services/text_fold.h), the one fold that "Enter filament" uses for the
//   same name, too.
// NOT-TO-DO: tolower(), strcasestr(): they follow the locale. A fold of every
//   byte (c | 0x20) takes 0x9C, the second byte of the capital U umlaut, for
//   0xBC, the small one's: one UTF-8 sequence would match another.
// No word (null or empty) is no search: every name holds it, a null name too.
// A word and a null or empty name: false.
bool colourSearchMatch(const char* name, const char* word);
