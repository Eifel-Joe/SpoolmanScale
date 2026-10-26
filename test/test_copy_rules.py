"""Compile the production copy rules and check what a copy takes over from its template behind Spoolman: the price of the spool and the price per kg that goes to the server, the empty weight, whether the template has a profile, the colour name and the supplier the card shows."""
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
  // Spool 2: 10.99 per kg on a 1015 g label is 11.15 for the spool.
  assert(fabsf(copyRulesSpoolPrice(10.99f, 1015) - 11.15f) < 0.001f);
  assert(copyRulesSpoolPrice(0.0f, 1000) == 0.0f);
  assert(copyRulesSpoolPrice(10.99f, 0) == 0.0f);
  // Untouched: the template's per-kg price exactly; typed: per kg of this label.
  assert(copyRulesCostPerKg(10.99f, 1015, 0.0f, false) == 10.99f);
  assert(fabsf(copyRulesCostPerKg(10.99f, 1015, 12.00f, true) - 11.82f) < 0.001f);
  assert(fabsf(copyRulesCostPerKg(10.99f, 1000, 12.00f, true) - 12.00f) < 0.001f);
  assert(copyRulesCostPerKg(10.99f, 1000, 0.0f, true) == 0.0f);   // emptied: no price
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
  // A price rounds half away from zero to the cent: 0.125 per kg on 1000 g is
  // 0.125, which is 0.13 and not 0.12 (the steps 125, 0.125 and 12.5 are all
  // exact in a float, so there is no error to tip the half).
  assert(fabsf(copyRulesSpoolPrice(0.125f, 1000) - 0.13f) < 0.001f);
  // No price, a negative one or one that is not a number is none.
  assert(copyRulesSpoolPrice(-1.0f, 1000) == 0.0f);
  assert(copyRulesSpoolPrice(NAN, 1000) == 0.0f);
  assert(copyRulesSpoolPrice(10.99f, -1015) == 0.0f);
  // A typed price has no weight to be divided by without a label weight, and
  // a negative or not-a-number one is no price either: all of them are none.
  assert(copyRulesCostPerKg(10.99f, 0, 12.00f, true) == 0.0f);
  assert(copyRulesCostPerKg(10.99f, 1000, -5.0f, true) == 0.0f);
  assert(copyRulesCostPerKg(10.99f, 1000, NAN, true) == 0.0f);
  // Untouched passes the template's value on whatever the label weight is: the
  // price is not worked out through the spool and back.
  assert(copyRulesCostPerKg(24.99f, 250, 0.0f, false) == 24.99f);
  assert(copyRulesCostPerKg(24.99f, 0, 0.0f, false) == 24.99f);
  assert(copyRulesCostPerKg(0.0f, 1000, 0.0f, false) == 0.0f);
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
