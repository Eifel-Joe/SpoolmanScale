"""Compile the production density rules and check which densities are plausible, and when a database entry is the product named on a Bambu tag: the vendor, the product name with or without a colour word after it, and the net weight where both name one."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/density_rules.h"
#include <cassert>
#include <cmath>
#include <cstddef>

int main() {
  static_assert(DENSITY_MIN_G_CM3 == 0.50f, "0.50 g/cm3 is the lightest");
  static_assert(DENSITY_MAX_G_CM3 == 4.00f, "4.00 g/cm3 is the heaviest");
  assert(densityPlausible(1.16f));
  assert(densityPlausible(0.50f) && densityPlausible(4.00f));
  assert(!densityPlausible(0.49f) && !densityPlausible(4.01f));
  assert(!densityPlausible(0.0f) && !densityPlausible(-1.2f) && !densityPlausible(NAN));
  assert(!densityPlausible(11.6f));   // a point typed in the wrong place
  const DensityProduct abs_s = { "Bambu Lab", "Support for ABS", 500 };
  assert(densityEntryIsProduct("Bambu Lab", "Support for ABS", 500, abs_s));
  assert(densityEntryIsProduct(" bambu lab ", "support FOR abs ", 500, abs_s));
  assert(densityEntryIsProduct("Bambu Lab", "Support for ABS", 0, abs_s));      // entry without a weight
  assert(!densityEntryIsProduct("Bambu Lab", "Support for ABS", 1000, abs_s));  // another size
  assert(!densityEntryIsProduct("Sunlu", "Support for ABS", 500, abs_s));
  assert(!densityEntryIsProduct("Bambu Lab", "Support for ABS-GF", 500, abs_s));
  assert(!densityEntryIsProduct("Bambu Lab", "ABS", 500, abs_s));
  const DensityProduct plapetg = { "Bambu Lab", "Support For PLA/PETG", 500 };
  assert(densityEntryIsProduct("Bambu Lab", "Support for PLA/PETG Black", 500, plapetg));
  assert(!densityEntryIsProduct("Bambu Lab", "Support for PLA", 500, plapetg));
  const DensityProduct none = { "Bambu Lab", "", 500 };
  assert(!densityEntryIsProduct("Bambu Lab", "", 500, none));
  assert(!densityEntryIsProduct(nullptr, nullptr, 500, abs_s));
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "density_rules.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("density rules: ok")
