"""Compile the production fold of one UTF-8 character and check it (V43): A-Z and the capitals of Latin-1 become their small letters, the multiplication sign and the sharp s stay, OE and Y diaeresis outside Latin-1 are folded too; a cut or broken sequence is one byte, as it is; the end of the text is 0 and goes nowhere. No locale, no tolower() (services/text_fold.h). The search in a list and the names that are one both read their text through it."""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"

# The tokenizer of the convention ratchet: comments and string contents
# blanked, so that a NOT-TO-DO comment may name the call it rejects. The
# script is not a package, so its folder goes on the path.
sys.path.insert(0, str(root / "scripts"))
import check_conventions as cc  # noqa: E402

# The rule is host-tested and must not depend on where it runs: no Arduino,
# no LVGL, nothing that follows a locale.
for rel in ("services/text_fold.h", "services/text_fold.cpp"):
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    raw = path.read_text(encoding="utf-8")
    includes = [line for line in raw.splitlines() if line.lstrip().startswith("#include")]
    for banned in ("Arduino.h", "lvgl.h", "lang.h", "<ctype.h>", "<cctype>", "<wctype.h>", "<cwctype>",
                   "<locale.h>", "<clocale>", "<locale>"):
        assert not any(banned in line for line in includes), f"src/{rel}: includes {banned} (the fold follows no locale)"
    code = cc.Source(raw).code
    for banned in ("tolower", "towlower", "toupper", "towupper", "setlocale"):
        assert not re.search(r"(?<![A-Za-z0-9_])" + banned + r"\s*\(", code), \
            f"src/{rel}: calls {banned}() (the fold follows no locale)"

source = r'''
#include "services/text_fold.h"
#include <cassert>
#include <cstring>

int main() {
  uint32_t c; const char* p;
  // The values of the plan (V43, Task 1).
  p = textFoldNext("A", &c);            assert(c == 'a' && *p == 0);
  p = textFoldNext("\xC3\x84", &c);     assert(c == 0xE4 && *p == 0);    // Ä -> ä
  p = textFoldNext("\xC3\xA4", &c);     assert(c == 0xE4);               // ä stays
  p = textFoldNext("\xC3\x9C", &c);     assert(c == 0xFC);               // Ü -> ü
  p = textFoldNext("\xC3\x89", &c);     assert(c == 0xE9);               // É -> é
  p = textFoldNext("\xC3\x97", &c);     assert(c == 0xD7);               // × stays
  p = textFoldNext("\xC3\x9F", &c);     assert(c == 0xDF);               // ß stays
  p = textFoldNext("\xC5\x92", &c);     assert(c == 0x153);              // Œ -> œ
  p = textFoldNext("\xC5\xB8", &c);     assert(c == 0xFF);               // Ÿ -> ÿ
  p = textFoldNext("\xC3", &c);         assert(c == 0xC3 && *p == 0);    // cut sequence: one byte
  p = textFoldNext("", &c);             assert(c == 0 && *p == 0);

  // The ASCII edges: only A to Z move, '@' and '[' next to them do not.
  p = textFoldNext("Z", &c);            assert(c == 'z');
  p = textFoldNext("z", &c);            assert(c == 'z');
  p = textFoldNext("@", &c);            assert(c == '@');
  p = textFoldNext("[", &c);            assert(c == '[');
  p = textFoldNext("5", &c);            assert(c == '5');
  // The Latin-1 edges: U+00C0 and U+00DE are the first and the last capital.
  p = textFoldNext("\xC3\x80", &c);     assert(c == 0xE0);               // À -> à
  p = textFoldNext("\xC3\x9E", &c);     assert(c == 0xFE);               // Þ -> þ
  p = textFoldNext("\xC3\xBF", &c);     assert(c == 0xFF);               // ÿ stays
  p = textFoldNext("\xC3\xB7", &c);     assert(c == 0xF7);               // ÷ stays
  p = textFoldNext("\xC5\x93", &c);     assert(c == 0x153);              // œ stays

  // One character at a time: the pointer comes back behind it.
  const char* text = "\xC3\x84" "b";
  p = textFoldNext(text, &c);           assert(c == 0xE4 && p == text + 2);
  p = textFoldNext(p, &c);              assert(c == 'b' && p == text + 3 && *p == 0);
  // Three bytes, unchanged: the euro sign.
  const char* euro = "\xE2\x82\xAC" "x";
  p = textFoldNext(euro, &c);           assert(c == 0x20AC && p == euro + 3);
  // At the end it stays where it is.
  p = textFoldNext(p + 1, &c);          assert(c == 0 && p == euro + 4);

  // A sequence that is broken or cut is one byte, as it is, and the next one
  // is read on its own: no byte is folded, none is skipped.
  const char* broken = "\xC3" "A";
  p = textFoldNext(broken, &c);         assert(c == 0xC3 && p == broken + 1);
  p = textFoldNext(p, &c);              assert(c == 'a' && *p == 0);
  const char* lone = "\x9C" "a";       // a continuation byte without its lead
  p = textFoldNext(lone, &c);           assert(c == 0x9C && p == lone + 1);
  const char* cut3 = "\xE2\x82";        // three bytes announced, two there
  p = textFoldNext(cut3, &c);           assert(c == 0xE2 && p == cut3 + 1);
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(src),
                    str(cpp), str(src / "services" / "text_fold.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("text fold: ok")
