#pragma once

#include <stdint.h>

// ============================================================
//  THE CASE FOLD OF TYPED TEXT: THE RULE (V43)
//
//  The search in a colour list (colourSearchMatch()) and the check for the
//  same name in "Enter filament" (sortEntrySame()) both compare text that
//  was typed with text that was stored. They read it through this one fold,
//  one character at a time, so that the two cannot drift apart: what the
//  search finds is what the entry calls the same.
//
//  The rule lives here without Arduino or LVGL, so a host test holds it to
//  the values in test_text_fold.py.
// ============================================================

// Reads the UTF-8 character at s and writes its folded code point to *folded;
// returns the pointer behind the character. The fold:
//   A-Z                      -> a-z
//   U+00C0 to U+00DE         -> +0x20, except U+00D7 (the multiplication sign)
//   U+0152 (OE ligature)     -> U+0153
//   U+0178 (Y diaeresis)     -> U+00FF
//   everything else          unchanged (the sharp s U+00DF, the division sign
//                            U+00F7 and every small letter stay as they are)
// A sequence that is broken, cut, overlong or a surrogate is one byte: the
// pointer moves one byte on and *folded is that byte, as it is. The next call
// reads on from there, so no byte is folded on its own and none is skipped.
// At the end of the text (*s is 0, or s is null) it returns s and writes 0.
// A null folded is allowed: the character is only stepped over.
// Wurzel: the keyboards have umlauts and accents now (QWERTZ, AZERTY), so a
//   word is typed with a capital umlaut or accent as often as with a small
//   one. The old fold took A-Z and left every other byte alone: the small u
//   umlaut typed met the capital one stored as two names, and "Enter
//   filament" would have made a second filament next to the first.
// Fix: decode the sequence, fold the code point, hand back the pointer.
// NOT-TO-DO: tolower(), towlower(), setlocale(): they follow the locale of
//   the platform, and the host test and the panel would fold differently. A
//   fold of every byte (c | 0x20) takes 0x9C, the second byte of the capital
//   U umlaut, for 0xBC, the small one's, and the multiplication sign for the
//   division sign: one sequence would match another. Dropping the dots or
//   spelling an umlaut out ("ue") is no fold either: the letter u is not the
//   letter u umlaut.
// A broken byte is its own value, so it can equal the code point of the same
// number (a lone 0xE9 and the small e acute): text here is UTF-8 from every
// source, and that case is not worth a second range of values.
// See test_text_fold.py, test_colour_search_rules.py, test_sort_entry_rules.py.
const char* textFoldNext(const char* s, uint32_t* folded);
