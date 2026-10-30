#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
//  THE WINDOW "CHOOSE PRINTER": THE KIND OF A PRINTER AND THE SEARCH (V47)
//
//  The printer chip of the AMS window opens a window with one button per
//  printer: the name above, under it the printer's kind. For a Bambu printer
//  that is its model as Bambuddy lists it ("A1 Mini", "X1C"); for a Klipper
//  printer it is the word "Klipper" with the layout and the number of its
//  tools or slots ("Klipper, MMU, 8 slots"). The magnifier of the window
//  keeps the buttons whose name or kind holds a word typed on the keyboard.
//
//  The rule lives here without Arduino or LVGL, so a host test holds it to
//  the values in test_printer_pick_rules.py; ui/printer_list.cpp is what
//  draws it on the panel. The words ("Klipper", "tools", "slots") are not
//  here: the window hands them in from the string table, so the rule needs no
//  lang.h and a language is a row of the table, not a change of this file.
//  Letters are compared in any case through colourSearchMatch(), the fold the
//  colour lists use (V43).
// ============================================================

// The kind of a printer, terminator included: 47 bytes and the terminator. The
// texts of the table are about 30 bytes at most.
#define PRINTER_ART_MAX  48

enum PrinterPickKind : uint8_t {
  PPK_BAMBU = 0,    // a printer of Bambuddy, FilaMan or an AMS source: the model, if there is one
  PPK_KLIPPER       // a Klipper printer stored on the scale: the word, the layout, the number
};

// What a Klipper printer's last connection test found. The rule has its own
// names, so it needs neither klipper_rules.h nor ams_slots.h; the window maps
// the list's numbers onto these. Meaningless for a Bambu printer.
enum PrinterPickLayout : uint8_t {
  PPL_SINGLE = 0,   // one field, the active spool
  PPL_TOOLCHANGER,  // tool macros, one spool per tool
  PPL_MMU,          // Happy Hare
  PPL_AFC           // Armored Turtle AFC
};

// One printer of the list, as the rule reads it. Text pointers may be null.
struct PrinterPickRow {
  const char*       name;
  PrinterPickKind   kind;
  const char*       model;    // Bambu only; null or empty when the backend names none
  PrinterPickLayout layout;   // Klipper only
  int               bays;     // Klipper only: tools or slots of the last test, 0 when unknown
};

// The words of the table. Each of the three formats holds exactly one %d,
// which gets the number of tools or slots (bays); nothing else is printed.
struct PrinterArtTexts {
  const char* klipper;     // "Klipper": a printer with one tool, or none counted
  const char* tools_fmt;   // "Klipper, %d tools"
  const char* mmu_fmt;     // "Klipper, MMU, %d slots"
  const char* afc_fmt;     // "Klipper, AFC, %d slots"
};

// Writes the kind of the printer into out (n bytes, terminator included):
//   Bambu:    the model, or an empty text when there is none (FilaMan and the
//             AMS source deliver none: the button then has the name only).
//   Klipper:  a tool changer from two tools on: tools_fmt;
//             an MMU or an AFC from one slot on: mmu_fmt or afc_fmt;
//             everything else, one tool or no number known included: klipper.
// The model means nothing for a Klipper printer, whatever stands there: the
// kind decides which of the two it is, not the other field. The number is
// bays and only bays, so one %d is all the table's texts need.
// The text is cut at n - 1 bytes when it does not fit and always ends in a
// terminator. The window passes PRINTER_ART_MAX, which holds every text of
// the table, so in the firmware nothing is cut. With n == 0 or out null
// nothing is written.
void printerPickArt(const PrinterPickRow& r, const PrinterArtTexts& t, char* out, size_t n);

// Whether the button of the printer stays in the list for the word: the name
// or the kind holds it anywhere (colourSearchMatch(): any case, the capital
// umlauts and accents included). art is the text printerPickArt() wrote for
// the same printer, which the window has at hand for the button's label; null
// means the name alone counts. No word (null or empty) shows every printer.
bool printerPickShows(const PrinterPickRow& r, const char* art, const char* word);
