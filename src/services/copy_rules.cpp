#include "services/copy_rules.h"

#include <math.h>

#include "services/spool_type_rules.h"   // spoolTypeOwnCore (U, L21)

float copyRulesCostPerKg(float tpl_cost_per_kg, float typed_price, bool typed) {
  // Untouched: the template's value as it is, not the cent the card shows.
  if (!typed) return tpl_cost_per_kg;
  // Typed: the price per kg exactly as typed, neither converted through the
  // label weight nor rounded. Zero empties the price, and a negative or
  // not-a-number price is no price (the negated comparison catches the
  // latter), nor is one the float ran over with.
  if (!(typed_price > 0.0f) || !isfinite(typed_price)) return 0.0f;
  return typed_price;
}

int copyRulesCore(int chosen_g, int tpl_core_g, bool tpl_inherited) {
  // A chosen type comes first and is trusted, even at 250 g, as in
  // spoolTypeCreateTare. Without one only an empty weight of the template's own
  // is kept (L21): an inherited one gives 0 and the field is left out.
  if (chosen_g > 0) return chosen_g;
  return spoolTypeOwnCore(tpl_core_g, tpl_inherited);
}

bool copyRulesHasProfile(const char* slicer_filament) {
  // Only the id counts, not slicer_filament_name: Bambuddy fills that in with
  // the name of the filament when the spool has no profile (Festlegung 14).
  return slicer_filament != nullptr && slicer_filament[0] != '\0';
}

const char* copyRulesColorName(const char* name, bool synthesized) {
  // A name built from the type is not the spool's own and stays off the copy.
  if (synthesized || name == nullptr) return "";
  return name;
}

int copyRulesSupplierShown(const bool* is_purchase_source, int n) {
  if (n <= 0) return -1;
  // No flags to read: nobody is marked, so the first supplier is the one.
  if (is_purchase_source == nullptr) return 0;
  for (int i = 0; i < n; i++) {
    if (is_purchase_source[i]) return i;
  }
  return 0;
}
