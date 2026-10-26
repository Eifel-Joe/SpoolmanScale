#pragma once

#include <stdint.h>

// ============================================================
//  WHAT A COPY TAKES OVER FROM ITS TEMPLATE: THE RULES
//
//  Source: design note J (docs/entwurf/2026-10-26-j-kopieren.md, "Entwurf J"),
//  spec proposal V24, and rule L21 (an empty weight that a spool only
//  inherited is not written onto the new one as its own).
//
//  A spool created at the scale as a copy of another one carries all fields
//  of its template, but only behind Bambuddy with its spools on a Spoolman
//  server (spoolTypesActive()); every other way copies as it always did. The
//  weights and the label come from the scale, and the note and the dates are
//  not taken over (Festlegung 3).
//
//  The rules live here, without Arduino, the backend, the network or the
//  screens: a host test holds them to the values in test_copy_rules.py, and
//  the code that reads the template, shows the card and creates the copy asks
//  this unit instead of keeping rules of its own.
//
//  What each rule is, and where the design note fixes it:
//
//    price        copyRulesCostPerKg is what goes to the server (Festlegung
//                 5): the template's own price per kg when the price button
//                 was not touched, else the price per kg typed on the card,
//                 exactly as typed. The card shows and takes the price per
//                 kg (section 3), so nothing is converted.
//    core         copyRulesCore is the empty weight the copy keeps as its
//                 own: a type chosen for it, else what U (V25) and L21 say
//                 about the template's.
//    profile      copyRulesHasProfile: only a preset id is a profile, and
//                 an empty one is none (Festlegung 14).
//    colour       copyRulesColorName: the template's own colour name, none
//                 when Bambuddy built it from the type (Festlegung 10).
//    supplier     copyRulesSupplierShown: the supplier the card shows beside
//                 the storage location (section 3).
//
//  CopyMissing names the parts of a copy that did not come across after the
//  spool was created (Festlegung 8). The spool stands once it is created;
//  what fails after that is reported, not undone. One bit per part, added up
//  with |:
//
//    CM_PRICE      the server took the spool but not the price (207, warning)
//    CM_PRESETS    the presets per printer model
//    CM_SUPPLIERS  the suppliers with article number, offer and purchase source
//    CM_KPROFILES  the calibration values (K profiles)
//    CM_FILAMENT   the filament id could not be read, the server looked one up
// ============================================================

enum CopyMissing : uint8_t { CM_NONE = 0, CM_PRICE = 1, CM_PRESETS = 2, CM_SUPPLIERS = 4, CM_KPROFILES = 8, CM_FILAMENT = 16 };

// The price per kg that goes to the server (Festlegung 5). Wurzel: the card
// used to show and take the price of the spool, and a typed one went back to
// one kg through the label weight, rounded to the cent (12.00 on a 1015 g
// label became 11.82), although what one types is a price per kg, and that
// does not change with how much filament the spool holds. Fix: the card
// shows and takes the price per kg. A typed price (typed true) is the copy's
// price per kg and goes on exactly as typed, neither converted nor rounded:
// 12.00 is 12.00, 12.345 is 12.345. Typed zero empties the price (0, no
// price), and so does a typed price that is negative, not a number or not
// finite. A button that was not touched (typed false) hands the template's
// value on as it is: a price per kg is the same for any label weight, so
// there is nothing to work out, and the cent the card shows is only shown.
// This rule rounds nothing; the request writes every price per kg to the
// cent (bambuddy_api.cpp), for every way a spool is created, which leaves a
// price typed on the pad (two decimals at most) as it is.
// NOT-TO-DO: dividing a typed price by the label weight, the rule before the
// acceptance; and handing the card's shown cent on in place of the
// template's own value for an untouched button.
float copyRulesCostPerKg(float tpl_cost_per_kg, float typed_price, bool typed);

// The empty weight the copy keeps as its own (L21 through U). A type chosen
// for the copy (chosen_g > 0) is its empty weight, also when it says Bambu's
// fixed 250 g, as spoolTypeCreateTare does. Otherwise spoolTypeOwnCore decides
// about the template's: an inherited one gives 0, which leaves the field out,
// else the template's own, never negative. A chosen_g of zero or less is no
// choice.
int copyRulesCore(int chosen_g, int tpl_core_g, bool tpl_inherited);

// True when the template has a profile: slicer_filament is a preset id. Wurzel:
// without a profile Bambuddy fills slicer_filament_name with the name of the
// filament (spool 14: no profile, name "ASA"), so the name says nothing.
// Fix: only the id counts, and an empty or null one is none. The caller then
// sends neither id nor name (Festlegung 14) and shows a dash.
bool copyRulesHasProfile(const char* slicer_filament);

// The colour name that goes to the new spool: the template's own, or "" (never
// null) when it has none or Bambuddy built it from the type
// (color_name_is_synthesized). A built name written to the copy would look like
// a chosen one. The result points at name or at a constant, so it lives as long
// as name does.
const char* copyRulesColorName(const char* name, bool synthesized);

// The index of the supplier the card shows: the first one marked as the
// purchase source, else the first one. -1 when there are none (n <= 0). A null
// is_purchase_source with n > 0 marks none and gives 0, so the first supplier
// is shown. Bambuddy allows one purchase source at most; with several marked,
// the first wins.
int copyRulesSupplierShown(const bool* is_purchase_source, int n);
