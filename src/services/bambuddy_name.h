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
