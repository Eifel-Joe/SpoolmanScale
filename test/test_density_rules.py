"""Compile the production density rules and check which densities are plausible, and when a database entry is the product named on a Bambu tag: the vendor, the product name with or without a colour word after it, and the net weight where both name one; which material a Bambu support code is listed under in the light form of the database, and which typical density a material is offered (o04, v0.8.7-beta.5)."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/density_rules.h"
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstring>

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
  const DensityProduct novendor = { "", "Support for ABS", 500 };
  assert(!densityEntryIsProduct("", "Support for ABS", 500, novendor));   // two missing makers are no maker

  // The light form lists by material: the base of each support code
  // (Niko11111 e1588eb, o04).
  assert(strcmp(densitySupportBase("ABS-S"), "ABS") == 0);
  assert(strcmp(densitySupportBase("pa-s"), "PA") == 0);
  assert(strcmp(densitySupportBase("PLA-S"), "PLA") == 0);
  assert(densitySupportBase("PLA") == nullptr);
  assert(densitySupportBase("PETG-S") == nullptr);
  assert(densitySupportBase("PLA-SX") == nullptr);
  assert(densitySupportBase("") == nullptr);
  assert(densitySupportBase(nullptr) == nullptr);

  // Typical values (Niko11111 b67f5ab, o04): exact names only, any case,
  // blanks at both ends ignored; a support code is not its base.
  assert(fabsf(densityTypical("PLA") - 1.24f) < 0.001f);
  assert(fabsf(densityTypical(" petg ") - 1.27f) < 0.001f);
  assert(fabsf(densityTypical("tpu-95a") - 1.22f) < 0.001f);
  assert(fabsf(densityTypical("PC-CF") - 1.36f) < 0.001f);
  assert(fabsf(densityTypical("PA12") - 1.10f) < 0.001f);
  assert(densityTypical("PLA-S") == 0.0f);
  assert(densityTypical("PAHT-CF") == 0.0f);
  assert(densityTypical("PEBA") == 0.0f);
  assert(densityTypical("") == 0.0f && densityTypical(nullptr) == 0.0f);
  // Every typical value is a plausible density: the card offers it as one.
  const char* all[] = { "PLA", "PLA+", "PLA-CF", "PETG", "PETG-CF", "PETG-GF", "PCTG", "PET-CF",
                        "ABS", "ABS+", "ABS-GF", "ASA", "ASA-CF", "TPU", "TPU-95A", "TPU-90A", "TPE",
                        "PC", "PC-CF", "PA", "PA6", "PA12", "PA6-CF", "HIPS", "PVA", "PVB", "PHA" };
  for (const char* m : all) assert(densityPlausible(densityTypical(m)));
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
