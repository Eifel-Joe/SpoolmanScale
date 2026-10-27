"""Compile the production rules for a spool created without a tag and without weighing and check them: when the entry is a no-tag entry and which sources it offers, that it reads no weight, when it asks for the spool type, the fill weight it takes (100 to 5000 g), what the created note says, and when "into AMS" is offered."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "app_config.h"
#include "services/no_tag_create_rules.h"
#include <cassert>
#include <cmath>

// The limits, spelled out here on purpose: the production defines must agree
// with this text, not with themselves. A new spool's tare is 30 g and up (from
// app_config.h), the type question below hangs on it.
static_assert(NEW_SPOOL_TARE_MIN_G == 30.0f, "a tare below 30 g is no spool");

int main() {
  // The filament a spool holds is 100 to 5000 g, both ends in.
  static_assert(NO_TAG_FILL_MIN_G == 100 && NO_TAG_FILL_MAX_G == 5000, "100 to 5000 g of filament");

  // The flag is on at the entry only when there is neither a Bambu tag nor an
  // id (a tag UID) on the pad: "create spool" and the copy of an archived spool
  // by number.
  assert(noTagModeAtEntry(false, false));
  assert(!noTagModeAtEntry(true, false) && !noTagModeAtEntry(false, true));

  // The sources the card offers (database, sort): in the flag always, without
  // it as before (no Bambu tag and an id there). A flag left on after a cancel
  // adds nothing to a Bambu tag's "not in BamBuddy" card (R1).
  assert(noTagSourceOffered(true, false, false));
  assert(noTagSourceOffered(false, false, true) && !noTagSourceOffered(false, true, true));
  assert(!noTagSourceOffered(false, false, false));
  assert(!noTagSourceOffered(true, true, false));

  // In the flag nothing is read off the scale, even with something on it.
  assert(noTagGross(true, 1300.0f) == 0.0f && noTagGross(false, 1300.0f) == 1300.0f);

  // The spool type is asked in BamBuddy, while none is chosen: in the flag
  // always, otherwise only for something on the pad (over 30 g; not a number
  // counts as an empty pad).
  assert(noTagTypeAsk(true, true, 0.0f, 0) && noTagTypeAsk(true, true, NAN, 0));
  assert(noTagTypeAsk(true, false, 30.1f, 0) && !noTagTypeAsk(true, false, 30.0f, 0));
  assert(!noTagTypeAsk(true, false, NAN, 0) && !noTagTypeAsk(false, true, 0.0f, 0));
  assert(!noTagTypeAsk(true, true, 0.0f, 216));   // a type already chosen

  // A fill weight of 0 or less is missing and asked for, in the flag only.
  assert(noTagFillMissing(true, 0) && noTagFillMissing(true, -1));
  assert(!noTagFillMissing(true, 1000) && !noTagFillMissing(false, 0));

  // What is typed in is 100 to 5000 g, both ends in; 0 and not a number are not.
  assert(noTagFillPlausible(100.0f) && noTagFillPlausible(5000.0f) && noTagFillPlausible(1000.0f));
  assert(!noTagFillPlausible(99.9f) && !noTagFillPlausible(5000.1f));
  assert(!noTagFillPlausible(0.0f) && !noTagFillPlausible(NAN));

  // The note after the spool is created: Bambu Lab (or Bambu), in any case and
  // with blanks around it, points at the chip; any other maker, none and a
  // null pointer do not.
  assert(noTagCreatedNote("Bambu Lab") == NTN_BAMBU_CHIP && noTagCreatedNote(" bambu lab ") == NTN_BAMBU_CHIP);
  assert(noTagCreatedNote("Bambu") == NTN_BAMBU_CHIP);
  assert(noTagCreatedNote("Tinmorry") == NTN_PLAIN && noTagCreatedNote("") == NTN_PLAIN);
  assert(noTagCreatedNote(nullptr) == NTN_PLAIN);

  // "Into AMS" under "more info": the server can assign a slot, an AMS is
  // there, a spool is on the screen that is not archived and has a number.
  assert(amsInfoOffered(true, true, true, false, 36));
  assert(!amsInfoOffered(false, true, true, false, 36) && !amsInfoOffered(true, false, true, false, 36));
  assert(!amsInfoOffered(true, true, false, false, 36) && !amsInfoOffered(true, true, true, true, 36));
  assert(!amsInfoOffered(true, true, true, false, 0));
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "no_tag_create_rules.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("no tag create rules: ok")
