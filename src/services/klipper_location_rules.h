#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
//  THE OLD LOCATION OF A SPOOL IN A KLIPPER PRINTER: THE RULES
//
//  V50 (Spec 5.3 Z, the bullet on the location, its part on the old one): when
//  the scale assigns a spool to a Klipper printer, the spool's location in
//  Spoolman becomes the printer's name. Its location before that is kept in
//  the spool's extra field scale_prev_location, and a spool that a later
//  assignment displaces from its field gets it back. This unit holds what is
//  remembered, what the displaced spool gets, and a text value as Spoolman's
//  extra field keeps it (JSON text: "Regal" goes as "\"Regal\"").
//
//  The rules live here, without Arduino, ArduinoJson or LVGL: a host test
//  holds them to the values in test_klipper_location_rules.py, and the device
//  part (klipper_location.cpp) asks this unit instead of keeping rules of its
//  own.
// ============================================================

// The key of the extra field on the spool. The scale makes the field once
// (text) when Spoolman does not know it yet.
#define KL_PREV_LOC_KEY  "scale_prev_location"

// A location with the terminator. Spoolman keeps at most 64 characters, which
// is 256 bytes of UTF-8 at the very most; the scale's own LOCATION_NAME_MAX is 130.
#define KL_LOC_MAX       260
// A location as the JSON text of the extra field, with the terminator: every
// quote and backslash doubled, and the two quotes around it.
#define KL_LOC_TEXT_MAX  (2 * KL_LOC_MAX + 2)

// What the spool that was displaced from the field gets.
enum KlipperRestoreAct : uint8_t {
  KR_NONE = 0,    // nothing: its location is no longer the printer's name
  KR_RESTORE,     // location = the remembered one, the extra field cleared
  KR_CLEAR        // location cleared and the extra field cleared: nothing was remembered
};

// The names of the stored Klipper printers (klipperStoreAt()), name[0..count).
// One argument instead of two: a function takes at most five (house rule).
struct KlipperNames {
  const char* const* name;
  int count;
};

// What goes into the extra field when the spool's location becomes the
// printer's name: its current location, also when that is empty (""), unless
// the current location is exactly (byte for byte) the name of one of the
// printers; then the value already remembered (stored_prev, null is "")
// stays, so that a spool that moves from one printer to another keeps the
// shelf it came from. A null current location is "", a null list or a null
// entry of it is no printer. False when out is null, n is 0 or the value does
// not fit n; out is then "".
bool klipperPrevToRemember(const char* current_location, const char* stored_prev,
                           KlipperNames printers, char* out, size_t n);

// What the displaced spool gets, from its location and its remembered value as
// they are in Spoolman now: KR_RESTORE when its location is exactly the
// printer's name and a value is remembered, KR_CLEAR when it is the printer's
// name and nothing (null or "") is remembered, KR_NONE for any other location
// (the user put the spool somewhere else meanwhile) and for an empty or null
// printer name.
KlipperRestoreAct klipperRestoreFor(const char* displaced_location, const char* printer_name,
                                    const char* stored_prev);

// A text as the JSON text of an extra field: a JSON string literal with its
// quotes, a quote and a backslash escaped, the control characters as \n, \t,
// \r, \b, \f or \u00XX; UTF-8 passes as it is. Null is "". False when out is
// null, n is 0 or the text with its terminator does not fit n; out is then "".
bool klipperExtraText(const char* value, char* out, size_t n);

// The inverse: the text of a JSON string literal, blanks around it allowed,
// every JSON escape decoded (\uXXXX and a pair of surrogates to UTF-8). False
// for anything else (null, not a string, unterminated, a raw control
// character, an unknown escape, half a pair of surrogates, \u0000) and when the
// text does not fit n; out is then "".
bool klipperExtraValue(const char* json_text, char* out, size_t n);
