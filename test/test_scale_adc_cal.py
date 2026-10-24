"""Compile the rule that decides when a calibration belongs to another ADC."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/scale_adc_cal.h"
#include <cassert>

int main() {
  // The same ADC as last time: the calibration is its own.
  assert(!scaleCalibrationStale("NAU7802", "NAU7802"));
  assert(!scaleCalibrationStale("HX711", "HX711"));
  // The other one: its counts per gram are not these.
  assert(scaleCalibrationStale("NAU7802", "HX711"));
  assert(scaleCalibrationStale("HX711", "NAU7802"));
  // Nothing recorded yet: every scale before this change had a NAU7802, so a
  // NAU7802 keeps its calibration and an HX711 does not inherit it.
  assert(!scaleCalibrationStale("", "NAU7802"));
  assert(scaleCalibrationStale("", "HX711"));
  assert(!scaleCalibrationStale(nullptr, "NAU7802"));
  assert(scaleCalibrationStale(nullptr, "HX711"));
  // No ADC found: nothing to compare, nothing to throw away.
  assert(!scaleCalibrationStale("NAU7802", "-"));
  assert(!scaleCalibrationStale("NAU7802", ""));
  assert(!scaleCalibrationStale("NAU7802", nullptr));
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"), str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("scale adc calibration: ok")
