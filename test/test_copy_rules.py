"""Compile the production copy rules and check what a copy takes over from its template behind Spoolman: the price per kg that goes to the server, the empty weight, whether the template has a profile, the colour name and the supplier the card shows."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/copy_rules.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <math.h>

// The bits of the parts a copy could not take over, spelled out here on
// purpose: the status line and the code that collects them must agree with
// this text, not with themselves. Each part has a bit of its own, so that
// they can be added up with |.
static_assert(CM_NONE == 0, "nothing is missing");
static_assert(CM_PRICE == 1, "bit 0");
static_assert(CM_PRESETS == 2, "bit 1");
static_assert(CM_SUPPLIERS == 4, "bit 2");
static_assert(CM_KPROFILES == 8, "bit 3");
static_assert(CM_FILAMENT == 16, "bit 4");
static_assert(sizeof(CopyMissing) == 1, "a byte, as the status of a copy keeps it");

int main() {
  // Spool 2, 10.99 per kg. Untouched: the template's price per kg exactly;
  // typed: the price per kg as typed, not worked through the label weight
  // (Festlegung 5: more filament on the spool does not change it).
  assert(copyRulesCostPerKg(10.99f, 0.0f, false) == 10.99f);
  assert(copyRulesCostPerKg(10.99f, 12.00f, true) == 12.00f);
  assert(copyRulesCostPerKg(10.99f, 12.345f, true) == 12.345f);   // not rounded either
  assert(copyRulesCostPerKg(10.99f, 0.0f, true) == 0.0f);   // emptied: no price
  // L21 through U's rule: a chosen type, else only an own empty spool.
  assert(copyRulesCore(200, 167, false) == 200);
  assert(copyRulesCore(0, 167, false) == 167);
  assert(copyRulesCore(0, 250, true) == 0);
  // A profile is a preset id, not the name Bambuddy fills in without one.
  assert(!copyRulesHasProfile(""));
  assert(!copyRulesHasProfile(nullptr));
  assert(copyRulesHasProfile("PFUS1ee7bf63b1a088"));
  assert(strcmp(copyRulesColorName("Black", false), "Black") == 0);
  assert(strcmp(copyRulesColorName("Basic", true), "") == 0);
  const bool src1[] = {false, true}, src2[] = {false, false};
  assert(copyRulesSupplierShown(src1, 2) == 1);
  assert(copyRulesSupplierShown(src2, 2) == 0);
  assert(copyRulesSupplierShown(src1, 0) == -1);

  // The edges around those, each worked out by hand.
  // A negative or not-a-number typed price is no price either.
  assert(copyRulesCostPerKg(10.99f, -5.0f, true) == 0.0f);
  assert(copyRulesCostPerKg(10.99f, NAN, true) == 0.0f);
  // A typed price stands on its own, also for a template without one.
  assert(copyRulesCostPerKg(0.0f, 12.00f, true) == 12.00f);
  // Untouched passes the template's value on as it is, more than two decimals
  // included, and a template without a price gives none.
  assert(copyRulesCostPerKg(24.99f, 0.0f, false) == 24.99f);
  assert(copyRulesCostPerKg(10.999f, 0.0f, false) == 10.999f);
  assert(copyRulesCostPerKg(0.0f, 0.0f, false) == 0.0f);
  // A chosen type of 0 or less is no choice.
  assert(copyRulesCore(-5, 167, false) == 167);
  assert(copyRulesCore(-5, 250, true) == 0);
  // No colour name at all is none as well.
  assert(strcmp(copyRulesColorName(nullptr, false), "") == 0);
  assert(strcmp(copyRulesColorName("", false), "") == 0);
  // The first purchase source is shown when more than one is marked; no flags
  // given but suppliers counted shows the first, and a negative count none.
  const bool src3[] = {false, true, true};
  assert(copyRulesSupplierShown(src3, 3) == 1);
  assert(copyRulesSupplierShown(nullptr, 2) == 0);
  assert(copyRulesSupplierShown(nullptr, 0) == -1);
  assert(copyRulesSupplierShown(src1, -1) == -1);
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "copy_rules.cpp"),
                    str(root / "src" / "services" / "spool_type_rules.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("copy rules: ok")
