"""Compile the production rule of the search in a colour list (V40) and check it: a name is shown when it holds the word, letters in any case, the capital umlauts and accents of Latin-1 included (V43, textFoldNext()), character by character, so that no UTF-8 sequence is taken for another one; no word shows every name (services/colour_search_rules.h)."""
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

  // Beyond ASCII a character is folded as a whole (V43, textFoldNext()): the
  // umlaut typed as it is written is found, and a capital umlaut or accent is
  // a small one now that the keyboard has them (QWERTZ, AZERTY). No byte of a
  // sequence is folded on its own, and nothing is spelled out: "u" is no "ü".
  assert(colourSearchMatch("Wei\xC3\x9F", "wei") && colourSearchMatch("Wei\xC3\x9F", "WEI\xC3\x9F"));
  assert(colourSearchMatch("Gr\xC3\xBCn", "gr\xC3\xBCn") && colourSearchMatch("GR\xC3\x9CN", "gr\xC3\x9Cn"));
  assert(colourSearchMatch("GR\xC3\x9CN", "gr\xC3\xBCn") && colourSearchMatch("Gr\xC3\xBCn", "GR\xC3\x9CN"));
  assert(colourSearchMatch("Gr\xC3\xBCn", "GR\xC3\x9C"));
  assert(colourSearchMatch("\xC3\x89" "cru", "\xC3\xA9"));
  assert(!colourSearchMatch("Grun", "\xC3\xBC"));
  assert(!colourSearchMatch("Gr\xC3\xBCn", "grun"));
  // 0xC3 0x97 (the multiplication sign) and 0xC3 0xB7 (the division sign), and
  // 0xC3 0x9F (sharp s) and 0xC3 0xBF (small y diaeresis), differ in the ASCII
  // case bit of their second byte (0x20) and are no pair of letters: a fold of
  // bytes would take the one for the other.
  assert(!colourSearchMatch("\xC3\x97", "\xC3\xB7") && !colourSearchMatch("\xC3\xB7", "\xC3\x97"));
  assert(!colourSearchMatch("\xC3\x9F", "\xC3\xBF") && !colourSearchMatch("\xC3\xBF", "\xC3\x9F"));
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
                    str(cpp), str(root / "src" / "services" / "colour_search_rules.cpp"),
                    str(root / "src" / "services" / "text_fold.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("colour search rules: ok")
