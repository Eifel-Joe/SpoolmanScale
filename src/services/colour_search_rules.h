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
//  both screens share on the panel.
// ============================================================

// The word, terminator included: 23 bytes, cut at a character boundary.
#define COLOUR_SEARCH_MAX  24

// True when name holds word anywhere: "2.0 Bone White" holds "bone", "BONE"
// and "e w", not "bonewhite". ASCII letters are compared in any case, every
// other byte as it is, so an umlaut typed as written is found and a capital
// umlaut is not a small one.
// Wurzel: the names are mostly English, and LVGL's keyboard has no umlauts;
//   case is what a typed word gets wrong.
// Fix: fold A-Z to a-z and nothing else.
// NOT-TO-DO: tolower(), strcasestr(): they follow the locale. A fold of every
//   byte (c | 0x20) takes 0x9C, the second byte of the capital U umlaut, for
//   0xBC, the small one's: one UTF-8 sequence would match another.
// No word (null or empty) is no search: every name holds it, a null name too.
// A word and a null or empty name: false.
bool colourSearchMatch(const char* name, const char* word);
