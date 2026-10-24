"""Compile the production spool parts rules and check which places count, what the mask sums and names, what is stored for a spool, and that a bare reading never goes below zero."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/spool_extras_rules.h"
#include <cassert>
#include <cmath>
#include <cstring>

// The limits, spelled out here on purpose: the production defines must agree
// with this text, not with themselves. Eight places, three of them on one
// spool, a name of 19 characters and a terminator, 200 g for one part, and
// 0xFF for "this spool follows the default".
static_assert(SPOOL_EXTRAS_MAX == 8, "eight places");
static_assert(SPOOL_EXTRAS_PICK == 3, "three picks");
static_assert(SPOOL_EXTRA_NAME_LEN == 20, "19 characters and the terminator");
static_assert(SPOOL_EXTRA_MAX_G == 200.0f, "200 g for one part");
static_assert(SPOOL_EXTRAS_FOLLOW_DEFAULT == 0xFF, "0xFF follows the default");

int main() {
  // Places 0, 2, 3 and 4 are used; 1 has no name, 5 to 7 were never set.
  // Place 1 still holds 7 g, as a place cleared in the settings can after a
  // backup is restored: only then does "an unused place weighs nothing" say
  // more than a plain sum.
  SpoolExtra list[SPOOL_EXTRAS_MAX] = {
    {"Halter", 3.0f}, {"", 7.0f}, {"Clip", 1.5f}, {"Ring", 10.0f}, {"Extra", 2.0f}
  };

  // A place counts when it has a name, and an index outside the eight is no
  // place at all (the list holds eight entries, so 8 must not be read).
  assert(extrasPlaceUsed(list, 0));
  assert(!extrasPlaceUsed(list, 1));
  assert(!extrasPlaceUsed(list, -1));
  assert(!extrasPlaceUsed(list, 8));

  // A mask keeps only used places, and of those only the first three: with
  // 0xFF that is 0, 2 and 3. A mask of a place that is not used comes to
  // nothing, a single pick stays whatever its index (4 is the fourth used
  // place, and alone it is within the limit of three).
  assert(extrasClampMask(list, 0xFF) == 0x0D);
  assert(extrasClampMask(list, 0x02) == 0);
  assert(extrasClampMask(list, 0x10) == 0x10);
  assert(extrasClampMask(list, 0x1D) == 0x0D);

  // 3 + 1.5 + 10 g, all exact in a float. A place that is not used weighs
  // nothing even with 7 g left in it, and no mask weighs nothing.
  assert(extrasMaskGrams(list, 0x0D) == 14.5f);
  assert(extrasMaskGrams(list, 0x02) == 0.0f);
  assert(extrasMaskGrams(list, 0) == 0.0f);

  // Names joined by " + ", empty for none.
  char buf[64];
  extrasMaskNames(list, 0x05, buf, sizeof(buf));
  assert(strcmp(buf, "Halter + Clip") == 0);
  extrasMaskNames(list, 0, buf, sizeof(buf));
  assert(strcmp(buf, "") == 0);
  // Into a buffer of eight: cut short with room for the terminator, and not
  // a byte written past it (the guard stays 'X').
  char guard[16];
  memset(guard, 'X', sizeof(guard));
  extrasMaskNames(list, 0x05, guard, 8);
  assert(strlen(guard) < 8);
  assert(strncmp(guard, "Halter", 6) == 0);
  for (int i = 8; i < (int)sizeof(guard); i++) assert(guard[i] == 'X');

  // A weight is 0 to 200 g: not a number, infinite and negative are 0.
  assert(extrasClampGrams(NAN) == 0.0f);
  assert(extrasClampGrams(INFINITY) == 0.0f);
  assert(extrasClampGrams(-1.0f) == 0.0f);
  assert(extrasClampGrams(3.0f) == 3.0f);
  assert(extrasClampGrams(250.0f) == 200.0f);

  // A name loses the blanks on both sides, and is cut to 19 characters.
  extrasTrimName(buf, SPOOL_EXTRA_NAME_LEN, "  Halter  ");
  assert(strcmp(buf, "Halter") == 0);
  char long_name[26];
  memset(long_name, 'a', 25);
  long_name[25] = '\0';
  extrasTrimName(buf, SPOOL_EXTRA_NAME_LEN, long_name);
  assert(strlen(buf) == 19);

  // What a spool takes off, from what is stored for it (default: place 0).
  // 0xFF: it follows the default. A choice naming a place that has been
  // cleared since comes to nothing, it does not fall back to the default. No
  // parts on purpose (0) stays no parts.
  assert(extrasResolve(list, 0xFF, 0x01) == 0x01);
  assert(extrasResolve(list, 0x04, 0x01) == 0x04);
  assert(extrasResolve(list, 0x02, 0x01) == 0);
  assert(extrasResolve(list, 0, 0x01) == 0);

  // What is stored for a choice. The default itself is stored as "follows",
  // anything else as it is, cut as extrasClampMask cuts it; none stays none
  // although the default is something. A pick equal to the default follows
  // it also when both are none: with no default, no parts is "follows" too.
  assert(extrasToStore(list, 0x01, 0x01) == 0xFF);
  assert(extrasToStore(list, 0x04, 0x01) == 0x04);
  assert(extrasToStore(list, 0x1D, 0) == 0x0D);
  assert(extrasToStore(list, 0, 0x01) == 0);
  assert(extrasToStore(list, 0, 0) == SPOOL_EXTRAS_FOLLOW_DEFAULT);

  // What the scale shows without the parts, and never less than nothing: a
  // shell lighter than its parts is 0 g, not minus one.
  assert(extrasBare(500.0f, 3.0f) == 497.0f);
  assert(extrasBare(2.0f, 3.0f) == 0.0f);
  assert(extrasBare(500.0f, 0.0f) == 500.0f);
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "spool_extras_rules.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("spool parts rules: ok")
