#include "services/copy_rules.h"

#include <math.h>

#include "services/spool_type_rules.h"   // spoolTypeOwnCore (U, L21)

// Half away from zero to two decimals, which is what roundf does at the half.
static float roundToCent(float x) {
  return roundf(x * 100.0f) / 100.0f;
}

float copyRulesSpoolPrice(float cost_per_kg, int label_g) {
  // The negated comparison also catches a price that is not a number.
  if (!(cost_per_kg > 0.0f) || !isfinite(cost_per_kg) || label_g <= 0) return 0.0f;
  const float spool = cost_per_kg * (float)label_g / 1000.0f;
  // A price so large that the float ran over is no price a card could show.
  return isfinite(spool) ? roundToCent(spool) : 0.0f;
}

float copyRulesCostPerKg(float tpl_cost_per_kg, int label_g, float typed_price, bool typed) {
  // Untouched: the template's value as it is. Going through the price of the
  // spool and back would round twice (24.99 per kg on 250 g becomes 25.00).
  if (!typed) return tpl_cost_per_kg;
  // Typed: the price of this spool taken back to one kg of its label. Zero
  // empties the price. Without a label weight there is nothing to divide by,
  // and a negative or not-a-number price is no price (the negated comparison
  // catches the latter).
  if (!(typed_price > 0.0f) || !isfinite(typed_price) || label_g <= 0) return 0.0f;
  const float per_kg = typed_price * 1000.0f / (float)label_g;
  return isfinite(per_kg) ? roundToCent(per_kg) : 0.0f;
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
