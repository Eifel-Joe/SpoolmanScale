#pragma once

// ============================================================
//  WHICH DENSITY A FILAMENT GETS: THE RULES
//
//  Spoolman needs a density to create a filament. The sources are asked in
//  order and the first that has one counts (V34): the filament database by
//  vendor and material, Spoolman's material list, the database entry of the
//  same vendor that carries the product name, and the density typed at the
//  scale. A Bambu tag "Support for ABS" (tag material ABS-S) finds no density
//  under its material: the database lists that product as material ABS
//  (density 1.16, net weight 500 g), so the product name has to find it.
//
//  The rules live here, without Arduino, the backend or the screens: a host
//  test holds them to the values in test_density_rules.py, and the screens
//  ask this unit instead of keeping rules of their own.
//
//  Two rules:
//
//    plausible   A density is a number from DENSITY_MIN_G_CM3 (0.50) to
//                DENSITY_MAX_G_CM3 (4.00) g/cm3, both ends in. Anything else
//                is no density: not a number, zero, a negative, and a value
//                far above the range (11.6 for 1.16, a point typed in the
//                wrong place).
//
//    product     A database entry is the product of the tag when its vendor
//                is the same, its name is the product name or the product
//                name, a blank and more text (a colour word, as in
//                "Support for PLA/PETG Black"), and its net weight is the
//                same where both name one. A longer word is another product:
//                "Support for ABS-GF" is not "Support for ABS".
// ============================================================

// The range of a plausible density in g/cm3, both ends included: 0.50 is the
// lightest filament, 4.00 the heaviest.
#define DENSITY_MIN_G_CM3 0.50f
#define DENSITY_MAX_G_CM3 4.00f

// A product as a Bambu tag names it: the vendor, the product name and the net
// weight in grams (0 when the tag names none).
struct DensityProduct {
  const char* vendor;
  const char* product;
  int net_weight_g;
};

// True when d is a plausible density: a number from DENSITY_MIN_G_CM3 to
// DENSITY_MAX_G_CM3, both ends in. Not a number is no density, nor is zero or
// anything outside the range.
bool densityPlausible(float d);

// True when the database entry is the product: the vendor is the same, the
// name is the product name or the product name, a blank and more text, and the
// net weight is the same where both name one (0 names none). Vendor and name
// are compared without case (ASCII only) and with the blanks at both ends
// ignored. A null vendor or name, an empty product name, and an empty vendor
// (after the blanks are ignored) on either side never match: two missing
// makers are not the same maker.
bool densityEntryIsProduct(const char* entry_vendor, const char* entry_name, int entry_weight_g, const DensityProduct& want);
