"""Compile the production spool colour helpers and check the colour as RGB for the label (w04): "242, 242, 232" from six or eight hex digits, a '#' in front and any case welcome, the alpha of eight digits dropped, a translucent filament's too; nothing for no colour, a text that is no hex colour, a clear filament (alpha 00, SPOOL_ALPHA_CLEAR: it has no colour of its own) or a buffer too small, and the buffer is then empty (services/spool_color.h)."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/spool_color.h"
#include <cassert>
#include <cstring>

// The buffer starts as garbage, so that a refusal that leaves it as it was shows.
static void stale(char* t) { strcpy(t, "stale"); }

int main() {
  // The values of the plan (w04, Task 4).
  char t[16];
  assert(spoolColorRgbText("F2F2E8", t, sizeof t) && strcmp(t, "242, 242, 232") == 0);
  assert(spoolColorRgbText("f2f2e8", t, sizeof t) && strcmp(t, "242, 242, 232") == 0);
  assert(spoolColorRgbText("#F2F2E8", t, sizeof t) && strcmp(t, "242, 242, 232") == 0);
  assert(spoolColorRgbText("F2F2E8FF", t, sizeof t) && strcmp(t, "242, 242, 232") == 0);
  assert(spoolColorRgbText("000000", t, sizeof t) && strcmp(t, "0, 0, 0") == 0);
  assert(spoolColorRgbText("FFFFFF", t, sizeof t) && strcmp(t, "255, 255, 255") == 0);
  stale(t);
  assert(!spoolColorRgbText("00000000", t, sizeof t) && t[0] == 0);   // clear
  stale(t);
  assert(!spoolColorRgbText("F2F2E800", t, sizeof t) && t[0] == 0);   // alpha 0
  assert(!spoolColorRgbText("", t, sizeof t) && !spoolColorRgbText(nullptr, t, sizeof t));
  assert(!spoolColorRgbText("F2F2E", t, sizeof t) && !spoolColorRgbText("red", t, sizeof t));
  assert(!spoolColorRgbText("FFFFFF", t, 13) && spoolColorRgbText("FFFFFF", t, 14));

  // Mixed case, and '#' with eight digits.
  assert(spoolColorRgbText("#f2F2e8", t, sizeof t) && strcmp(t, "242, 242, 232") == 0);
  assert(spoolColorRgbText("#F2F2E8FF", t, sizeof t) && strcmp(t, "242, 242, 232") == 0);
  assert(spoolColorRgbText("0A141E", t, sizeof t) && strcmp(t, "10, 20, 30") == 0);
  // Translucent is still a colour: its first six digits, whatever the alpha
  // as long as it is not 00. Only a clear filament (alpha 00) has none.
  assert(spoolColorRgbText("F2F2E880", t, sizeof t) && strcmp(t, "242, 242, 232") == 0);
  assert(spoolColorRgbText("#F2F2E801", t, sizeof t) && strcmp(t, "242, 242, 232") == 0);
  stale(t);
  assert(!spoolColorRgbText("#F2F2E800", t, sizeof t) && t[0] == 0);
  // Every refusal leaves the buffer empty.
  stale(t);
  assert(!spoolColorRgbText("", t, sizeof t) && t[0] == 0);
  stale(t);
  assert(!spoolColorRgbText(nullptr, t, sizeof t) && t[0] == 0);
  stale(t);
  assert(!spoolColorRgbText("F2F2E", t, sizeof t) && t[0] == 0);
  stale(t);
  assert(!spoolColorRgbText("F2F2E8F", t, sizeof t) && t[0] == 0);   // seven digits
  stale(t);
  assert(!spoolColorRgbText("F2F2E8FF0", t, sizeof t) && t[0] == 0); // nine
  stale(t);
  assert(!spoolColorRgbText("GGGGGG", t, sizeof t) && t[0] == 0);
  stale(t);
  assert(!spoolColorRgbText("red", t, sizeof t) && t[0] == 0);
  stale(t);
  assert(!spoolColorRgbText("#", t, sizeof t) && t[0] == 0);
  // Too small by one: "255, 255, 255" is 13 bytes and its terminator.
  stale(t);
  assert(!spoolColorRgbText("FFFFFF", t, 13) && t[0] == 0);
  assert(spoolColorRgbText("FFFFFF", t, 14) && strcmp(t, "255, 255, 255") == 0);
  assert(!spoolColorRgbText("000000", t, 5) && t[0] == 0);
  assert(spoolColorRgbText("000000", t, 8) && strcmp(t, "0, 0, 0") == 0);
  // No room at all: nothing is written.
  stale(t);
  assert(!spoolColorRgbText("FFFFFF", t, 0) && strcmp(t, "stale") == 0);
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "spool_color.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("spool color rgb: ok")
