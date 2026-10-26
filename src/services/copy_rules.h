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
//    price        copyRulesSpoolPrice is what the card shows on the price
//                 button: the template's price per kg times the label weight
//                 (section 3, Festlegung 5). copyRulesCostPerKg is what goes
//                 to the server (Festlegung 5): the template's own price per
//                 kg when the button was not touched, else the typed price of
//                 the spool taken back to one kg of this label.
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

// The price of the new spool, for the button on the card: the template's price
// per kg times the label weight in grams over 1000, rounded to the cent (half
// away from zero). Spool 2: 10.99 per kg on a 1015 g label is 11.15. No price
// (zero, negative, not a number, infinite), no label weight (zero or less) and
// a result that is not finite give 0.
float copyRulesSpoolPrice(float cost_per_kg, int label_g);

// The price per kg that goes to the server. Wurzel: the card shows the price
// of the spool, but the server keeps a price per kg; working the template's
// price through the spool and back rounds twice (24.99 per kg on a 250 g label
// shows 6.25 and would come back as 25.00). Fix: a button that was not touched
// (typed false) hands the template's value on as it is, whatever the label
// weight. A typed price is the price of the spool and goes back to one kg of
// this label, rounded to the cent: 12.00 on a 1015 g label is 11.82 per kg.
// Typed zero empties the price (0, no price), and so does a typed price that
// is negative or not a number, or a label weight of zero or less to divide it
// by. NOT-TO-DO: sending copyRulesSpoolPrice back through the label weight
// for an untouched button, which is the double rounding above.
float copyRulesCostPerKg(float tpl_cost_per_kg, int label_g, float typed_price, bool typed);

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
