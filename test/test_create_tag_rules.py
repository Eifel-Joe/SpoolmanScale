"""Compile the production rules for the tag a create card belongs to (Punkteliste n04) and check them: a card opened for a tag stays while that tag, or none at all, is read, and goes when another one is; a card without a tag (a create without one) never goes for a tag. The key of a tag is its Bambu tray uuid (both chips of a spool share it), else the reader's UID. A card for a Bambu spool does not go for a 4-byte UID whose Bambu read has not brought the tray uuid yet: that is the other chip of the same spool, or the same chip read again, and the next try brings the uuid."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/create_tag_rules.h"
#include <cassert>
#include <cstring>

int main() {
  const char* bambu = "0123456789ABCDEF0123456789ABCDEF";   // 32 characters
  const char* other_bambu = "FEDCBA9876543210FEDCBA9876543210";
  const char* chip_a = "7B:40:86:F6";                       // a Bambu chip, 4 bytes
  const char* chip_b = "11:22:33:44";
  const char* ntag = "04:3E:1F:28:CF:2A:81";               // 7 bytes
  const char* other_ntag = "04:CF:25:28:CF:2A:81";

  // The key: a Bambu tag by its tray uuid, which both chips of a spool share,
  // whatever chip the reader saw; any other tag by the reader's UID.
  assert(strcmp(createPadTagKey(bambu, chip_a), bambu) == 0);
  assert(strcmp(createPadTagKey(bambu, chip_b), bambu) == 0);
  assert(strcmp(createPadTagKey("", ntag), ntag) == 0);
  assert(strcmp(createPadTagKey("0123", ntag), ntag) == 0);   // not 32 characters: no Bambu uuid
  assert(strcmp(createPadTagKey(nullptr, ntag), ntag) == 0);
  assert(strcmp(createPadTagKey(nullptr, nullptr), "") == 0);

  // A card for a Bambu spool.
  assert(createCardTagFits(bambu, bambu, chip_a));            // the same spool, either chip
  assert(createCardTagFits(bambu, bambu, chip_b));
  assert(createCardTagFits(bambu, "", ""));                  // nothing read now
  assert(createCardTagFits(bambu, nullptr, nullptr));
  assert(createCardTagFits(bambu, "", chip_b));              // a 4-byte read without its uuid yet
  assert(!createCardTagFits(bambu, other_bambu, chip_b));    // another Bambu spool, read in full
  assert(!createCardTagFits(bambu, "", ntag));               // an NTAG: another spool
  // A card for an NTAG.
  assert(createCardTagFits(ntag, "", ntag));
  assert(createCardTagFits(ntag, "", ""));
  assert(!createCardTagFits(ntag, "", other_ntag));
  assert(!createCardTagFits(ntag, "", chip_a));              // a Bambu chip, not read in full yet
  assert(!createCardTagFits(ntag, bambu, chip_a));
  // A card for a Creality tag (4 bytes, no tray uuid): the UID counts.
  assert(createCardTagFits(chip_a, "", chip_a));
  assert(!createCardTagFits(chip_a, "", chip_b));
  // A card without a tag stays for any tag: what lies on the pad is not weighed.
  assert(createCardTagFits("", "", ntag));
  assert(createCardTagFits(nullptr, bambu, chip_a));
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "create_tag_rules.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("create tag rules: ok")
