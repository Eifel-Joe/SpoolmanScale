#pragma once

#include <stdint.h>

#include "services/klipper_location_rules.h"   // KlipperNames

// ============================================================
//  KLIPPER PRINTERS LEFT OUT OF THE LOCATION PICKER: THE RULE
//
//  V79 (Spec 5.3 Z, the bullet on the location, "Nicht zur Auswahl"): a spool
//  the scale puts into a Klipper printer gets the printer's name as its
//  location, so Spoolman lists the printer among its locations and Bambuddy
//  copies it into its own list. The scale's location picker leaves those
//  names out; on the spool, in Spoolman and in Bambuddy the location stays
//  as it is. Happy Hare in push/pull writes a location of its own per gate,
//  "<host> @ MMU Gate:<n>" (Happy Hare components/mmu_server.py); with a
//  printer the last connection test found to be Happy Hare those go too.
//
//  Without Arduino, ArduinoJson or LVGL: test_klipper_hide_rules.py holds it
//  to its values, and the picker (more_info_screen.cpp) asks it for every row.
// ============================================================

// What the picker leaves out: the names of the stored printers (a null list
// or a null entry is no printer) and whether one of them is Happy Hare.
// Empty ({{nullptr, 0}, false}) where the scale has no Klipper printers.
struct KlipperHide {
  KlipperNames printers;
  bool mmu;
};

// True when the location is the name of one of the printers, compared the way
// the Klipper page compares two printer names (klipperNameTaken(): case and
// accents do not count) after blanks at both ends are dropped; or, with
// hide.mmu, when it is Happy Hare's form: at least one character, then
// " @ MMU Gate:" and one or more digits up to the end, blanks at both ends
// dropped. A location that only contains a printer's name stays. Null, ""
// and blanks only are never left out.
bool klipperLocationHidden(const char* location, const KlipperHide& hide);

// One location of the picker's list, in order.
enum KlipperLocRow : uint8_t {
  KLR_SHOW = 0,   // a row
  KLR_HIDE,       // left out, and it does not count against the limit
  KLR_CUT         // the list is full (shown >= limit): stop and say so
};

// shown is the number of rows the picker has made so far, limit its most.
// Left out comes first, so a printer behind the limit neither takes a place
// nor makes the picker say that locations are missing.
KlipperLocRow klipperLocationRow(const char* location, const KlipperHide& hide,
                                 int shown, int limit);
