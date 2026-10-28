"""Compile the production automatic sending rules and check that the verdict is taken on the reading without the parts on the spool: a write, a held-back rise, or no change."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/auto_send_rules.h"
#include <cassert>
#include <cmath>

// Two weights are the same when they differ by less than a hundredth of a
// gram: the sums below are exact in a float only by luck.
static bool near(float a, float b) { return fabsf(a - b) < 0.01f; }

// The caller of autoSendVerdict passes gross, parts, spool and stored in
// grams; the spool weighs 200 g, the backend holds 800 g, and the one part on
// it (the holder) weighs 3 g, unless a line says otherwise.
int main() {
  // Without the part the reading would be a write (803) or a rise (807.5).
  AutoSendVerdict v = autoSendVerdict(1003.0f, 3.0f, 200.0f, 800.0f);
  assert(v.kind == AUTO_SEND_UNCHANGED && near(v.netto_g, 800.0f));
  v = autoSendVerdict(1007.5f, 3.0f, 200.0f, 800.0f);
  assert(v.kind == AUTO_SEND_WRITE && near(v.netto_g, 804.5f) && near(v.diff_g, 4.5f));
  // More than 5 g over the backend is held back; exactly 5 g is written.
  assert(autoSendVerdict(1010.0f, 3.0f, 200.0f, 800.0f).kind == AUTO_SEND_RISE);
  assert(autoSendVerdict(1008.0f, 3.0f, 200.0f, 800.0f).kind == AUTO_SEND_WRITE);
  // Under 1 g either way is the same value; 1 g is a change.
  assert(autoSendVerdict(1002.2f, 3.0f, 200.0f, 800.0f).kind == AUTO_SEND_UNCHANGED);
  assert(autoSendVerdict(1004.0f, 3.0f, 200.0f, 800.0f).kind == AUTO_SEND_WRITE);
  // Lighter is always written.
  v = autoSendVerdict(950.0f, 3.0f, 200.0f, 800.0f);
  assert(v.kind == AUTO_SEND_WRITE && near(v.netto_g, 747.0f));
  // Never below 0: parts and spool heavier than the reading.
  v = autoSendVerdict(150.0f, 3.0f, 200.0f, 50.0f);
  assert(v.kind == AUTO_SEND_WRITE && v.netto_g == 0.0f);
  assert(autoSendVerdict(2.0f, 3.0f, 0.0f, 0.0f).kind == AUTO_SEND_UNCHANGED);
  // No part: the original's rule unchanged.
  assert(autoSendVerdict(1007.0f, 0.0f, 200.0f, 800.0f).kind == AUTO_SEND_RISE);
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "auto_send_rules.cpp"),
                    str(root / "src" / "services" / "spool_extras_rules.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("auto send rules: ok")
