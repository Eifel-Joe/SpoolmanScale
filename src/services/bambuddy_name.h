#pragma once

#include <stddef.h>

// ============================================================
//  THE NAME BAMBUDDY KEEPS FOR A FILAMENT
//
//  With BamBuddy in front of Spoolman, BamBuddy reads a filament's line out
//  of its name: the material in front is taken away and what is left is the
//  line, the whole name when it does not start with the material. On every
//  spool edit that goes through BamBuddy it then writes the filament's name
//  anew as "<material> <line>".
//
//  So a filament the scale creates as "Test Mint" turns into "ABS Test Mint"
//  the first time BamBuddy touches one of its spools: the line becomes "Test
//  Mint", and the labels show the line and the colour name twice. A name
//  already in the form "<material> <line>" is what BamBuddy keeps as it is
//  (the material is taken away, the line is put back behind it), so the scale
//  creates its filaments in that form from the start, for all three ways of
//  creating one: an entered sort, a label, a new filament from the database.
//
//  Under that name two filaments of one maker, material and line are told
//  apart by their colour alone (before, the colour's name was part of the
//  filament's name), so a filament is found again by its hue, see
//  bambuddyColourFits().
//
//  Pure text, without Arduino, LVGL or a backend: a host test holds it to the
//  values in test_bambuddy_name.py, among them the round trip through the
//  rule above.
// ============================================================

// The name BamBuddy gives a filament and keeps - the material, then the line
// when there is one ("ABS", "ABS Matte", "PLA Tough+"). Blanks around the
// material and the line are dropped; an empty line or one of only blanks
// gives the material alone. The name is cut to out_size - 1 bytes at a UTF-8
// boundary, never through a sequence.
void bambuddyFilamentName(const char* material, const char* line, char* out, size_t out_size);

// Spoolman keeps a clear filament as 00000000: no hue, alpha 00; a see-through
// one is a hue with alpha 00. True for eight digits that end in 00. Six digits
// alone cannot tell a clear filament from a black one.
bool isClearHex(const char* hex);

// Whether a filament of the inventory has the colour that is wanted, the way
// Spoolman keeps it: fil_hex is its color_hex (six digits, perhaps an alpha
// pair behind them; a clear filament is 00000000, alpha 00) and fil_multi its
// multi_color_hexes ("9CDBD9,FFFFFF"), either one empty or null when the
// filament has none. Behind BamBuddy the name no longer tells colours apart
// ("ABS Basic" is every colour of that line), so the colour has to.
//  - want_multi (two or more colours): the filament's list is the same colours
//    in the same order. The order counts, it is the direction of a gradient.
//  - else want_clear: the filament has no list and its colour is a clear one.
//  - else want_hex: the filament has no list, its colour is no clear one and
//    its first six digits are want_hex's.
// A '#' in front of a colour and the case of the digits make no difference;
// an alpha pair behind six digits is not compared. A filament with nothing to
// compare (null or empty colour) fits no wanted colour.
bool bambuddyColourFits(const char* fil_hex, const char* fil_multi, bool want_clear,
                        const char* want_hex, const char* want_multi);
