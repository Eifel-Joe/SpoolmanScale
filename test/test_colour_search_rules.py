"""Compile the production rule of the search in a colour list (V40) and check it: a name is shown when it holds the word, ASCII letters in any case, every other byte as it is, so that no UTF-8 sequence is folded into another one; no word shows every name (services/colour_search_rules.h)."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/colour_search_rules.h"
#include <cassert>

// The limit, spelled out here on purpose: the production define must agree
// with this text, not with itself. A word of 23 bytes and a terminator.
static_assert(COLOUR_SEARCH_MAX == 24, "a word of 23 bytes");

int main() {
  // The acceptance of V40: Sunlu PLA, "bone".
  assert(colourSearchMatch("2.0 Bone White", "bone"));
  assert(!colourSearchMatch("2.0 Black", "bone") && !colourSearchMatch("White", "bone"));

  // ASCII letters in any case, on either side; digits, blanks and signs as they are.
  assert(colourSearchMatch("2.0 Bone White", "BONE") && colourSearchMatch("2.0 BONE WHITE", "Bone"));
  assert(colourSearchMatch("2.0 Bone White", "2.0") && colourSearchMatch("2.0 Bone White", "e w"));
  assert(colourSearchMatch("Black (Basic)", "(basic)"));
  assert(!colourSearchMatch("2.0 Bone White", "bonewhite") && !colourSearchMatch("Light-Green", "light green"));

  // Anywhere in the name: at the start, in the middle, at the end, the whole of it.
  assert(colourSearchMatch("Bone White", "bone") && colourSearchMatch("Bone White", "white"));
  assert(colourSearchMatch("Bone White", "e wh") && colourSearchMatch("Bone White", "bone white"));
  // A part that matched and broke off starts over one byte on: "bbone", "bobone".
  assert(colourSearchMatch("bbone", "bone") && colourSearchMatch("Bobone", "bone"));
  // Not past the end, and not longer than the name.
  assert(!colourSearchMatch("Bone White", "whites") && !colourSearchMatch("Bone", "bone white"));

  // No word, an empty one or a null one: every name, a null name too.
  assert(colourSearchMatch("Bone White", "") && colourSearchMatch("Bone White", nullptr));
  assert(colourSearchMatch("", "") && colourSearchMatch(nullptr, "") && colourSearchMatch(nullptr, nullptr));
  // A word and no name: nothing.
  assert(!colourSearchMatch(nullptr, "bone") && !colourSearchMatch("", "bone"));

  // Bytes beyond ASCII are compared as they are: the umlaut typed as it is
  // written is found, a capital one is not a small one, and no byte of a
  // sequence is folded. 0xC3 0x9C is the capital U umlaut, 0xC3 0xBC the small
  // one: 0x9C with the ASCII case bit (0x20) is 0xBC, so a fold that does not
  // stop at ASCII would take the one for the other.
  assert(colourSearchMatch("Wei\xC3\x9F", "wei") && colourSearchMatch("Wei\xC3\x9F", "WEI\xC3\x9F"));
  assert(colourSearchMatch("Gr\xC3\xBCn", "gr\xC3\xBCn") && colourSearchMatch("GR\xC3\x9CN", "gr\xC3\x9Cn"));
  assert(!colourSearchMatch("GR\xC3\x9CN", "gr\xC3\xBCn") && !colourSearchMatch("Gr\xC3\xBCn", "GR\xC3\x9CN"));
  assert(!colourSearchMatch("Gr\xC3\xBCn", "grun"));
  // Nor does a byte beyond ASCII meet a letter: 0xC1 is 'A' with the high bit.
  assert(!colourSearchMatch("\xC1", "a") && !colourSearchMatch("\xE1", "A") && !colourSearchMatch("a", "\xC1"));
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "colour_search_rules.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("colour search rules: ok")
