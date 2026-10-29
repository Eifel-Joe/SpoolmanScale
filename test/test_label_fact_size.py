"""Compile the production rule that picks the size of the label's fact lines (V45) and check it: of the candidates, in the order of preference, the first one wins that the font can spell, whose widest line (bold stroke included) fits the width, whose lines fit the height, and that leaves a code at least as large as the small lines did; none fits, then -1 (services/label_fact_size.h)."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/label_fact_size.h"
#include <cassert>
#include <stdint.h>

// The candidates are built by these two, with the numbers as plain ints, so
// that no brace initialisation narrows and no field is left out.
static LabelFactFit mkFit(int width, int height, int code, bool spells) {
  LabelFactFit f{};
  f.width = (int16_t)width;
  f.height = (int16_t)height;
  f.code = (int16_t)code;
  f.spells = spells;
  return f;
}

static LabelFactRoom mkRoom(int width, int height, int code_min) {
  LabelFactRoom r{};
  r.width = (int16_t)width;
  r.height = (int16_t)height;
  r.code_min = (int16_t)code_min;
  return r;
}

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

int main() {
  // Joerg's label (Brother 300 dpi, 29 mm upright, 247 dots for the text, a
  // label without a code first): the one-line candidates 36, 32, 30, 28 and
  // 26 px, the widest line of each with its bold stroke. The heights all
  // fit the 300 dots of the area, so the width decides.
  const LabelFactFit lines[] = {
    mkFit(297, 290, 0, true),     // 36 px
    mkFit(265, 270, 0, true),     // 32 px
    mkFit(249, 255, 0, true),     // 30 px: two dots too wide
    mkFit(233, 240, 0, true),     // 28 px
    mkFit(218, 225, 0, true),     // 26 px
  };
  assert(labelFactPick(lines, COUNT(lines), mkRoom(247, 300, 0)) == 3);   // 28 px, index 3
  // The width is the whole of it: each candidate is the pick in a room as
  // wide as its line, and the next one in a room one dot narrower.
  assert(labelFactPick(lines, COUNT(lines), mkRoom(297, 300, 0)) == 0);
  assert(labelFactPick(lines, COUNT(lines), mkRoom(296, 300, 0)) == 1);
  assert(labelFactPick(lines, COUNT(lines), mkRoom(265, 300, 0)) == 1);
  assert(labelFactPick(lines, COUNT(lines), mkRoom(264, 300, 0)) == 2);
  assert(labelFactPick(lines, COUNT(lines), mkRoom(249, 300, 0)) == 2);
  assert(labelFactPick(lines, COUNT(lines), mkRoom(248, 300, 0)) == 3);
  assert(labelFactPick(lines, COUNT(lines), mkRoom(233, 300, 0)) == 3);
  assert(labelFactPick(lines, COUNT(lines), mkRoom(232, 300, 0)) == 4);
  assert(labelFactPick(lines, COUNT(lines), mkRoom(218, 300, 0)) == 4);
  // All too wide: none.
  assert(labelFactPick(lines, COUNT(lines), mkRoom(217, 300, 0)) == -1);
  assert(labelFactPick(lines, COUNT(lines), mkRoom(100, 300, 0)) == -1);
  // The height decides in the same way: 290 is the 36 px, 270 the 32 px.
  assert(labelFactPick(lines, COUNT(lines), mkRoom(400, 290, 0)) == 0);
  assert(labelFactPick(lines, COUNT(lines), mkRoom(400, 289, 0)) == 1);
  assert(labelFactPick(lines, COUNT(lines), mkRoom(400, 224, 0)) == -1);

  // The same label with a code: a candidate that is too wide has no code
  // measured (0); the ones that fit carry the code that stays. The small
  // lines left a code of 150 dots, so the 28 px, which leaves 150, is as
  // good as they were; the 26 px leaves more.
  const LabelFactFit coded[] = {
    mkFit(297, 290, 0, true),     // 36 px: too wide, no code measured
    mkFit(265, 270, 0, true),     // 32 px: too wide
    mkFit(249, 255, 0, true),     // 30 px: too wide
    mkFit(233, 240, 150, true),   // 28 px
    mkFit(218, 225, 160, true),   // 26 px
  };
  assert(labelFactPick(coded, COUNT(coded), mkRoom(247, 300, 150)) == 3);   // the code is not smaller
  // A code smaller than the small lines left: skipped, the next one is taken.
  assert(labelFactPick(coded, COUNT(coded), mkRoom(247, 300, 151)) == 4);
  assert(labelFactPick(coded, COUNT(coded), mkRoom(247, 300, 160)) == 4);
  assert(labelFactPick(coded, COUNT(coded), mkRoom(247, 300, 161)) == -1);
  // A label without a code asks for none (code_min 0); a candidate with no
  // code measured (0) meets that.
  assert(labelFactPick(coded, COUNT(coded), mkRoom(247, 300, 0)) == 3);

  // A candidate whose font cannot spell a letter of the label is skipped,
  // however well its numbers fit. A width of 0 is how that is reported, and
  // it must not win on width and height alone.
  const LabelFactFit spelled[] = {
    mkFit(233, 240, 0, false),    // would fit
    mkFit(218, 225, 0, true),
  };
  assert(labelFactPick(spelled, COUNT(spelled), mkRoom(247, 300, 0)) == 1);
  const LabelFactFit none_spelled[] = { mkFit(233, 240, 0, false), mkFit(218, 225, 0, false) };
  assert(labelFactPick(none_spelled, COUNT(none_spelled), mkRoom(247, 300, 0)) == -1);
  const LabelFactFit zero[] = { mkFit(0, 0, 0, false), mkFit(218, 225, 0, true) };
  assert(labelFactPick(zero, 1, mkRoom(247, 300, 0)) == -1);
  assert(labelFactPick(zero, COUNT(zero), mkRoom(247, 300, 0)) == 1);

  // The order of preference is the order of the list: the first that passes
  // wins, though a later one would be larger.
  const LabelFactFit order[] = {
    mkFit(218, 225, 0, true),     // passes
    mkFit(233, 240, 0, true),     // passes too, and is the larger text
  };
  assert(labelFactPick(order, COUNT(order), mkRoom(247, 300, 0)) == 0);
  // Each of the four conditions alone turns a candidate down, by one unit;
  // the fifth passes, and the one after it, better in every number, is not
  // reached.
  const LabelFactFit turned_down[] = {
    mkFit(248, 100, 150, true),   // one dot too wide
    mkFit(247, 301, 150, true),   // one dot too high
    mkFit(247, 100, 149, true),   // a code one dot too small
    mkFit(247, 100, 150, false),  // no glyph for a letter
    mkFit(247, 300, 150, true),   // passes, on every edge
    mkFit(100, 50, 200, true),
  };
  assert(labelFactPick(turned_down, COUNT(turned_down), mkRoom(247, 300, 150)) == 4);
  // The edges of that last one, singly: equal passes, one off does not.
  const LabelFactFit edge = mkFit(247, 300, 150, true);
  assert(labelFactPick(&edge, 1, mkRoom(247, 300, 150)) == 0);
  assert(labelFactPick(&edge, 1, mkRoom(246, 300, 150)) == -1);
  assert(labelFactPick(&edge, 1, mkRoom(247, 299, 150)) == -1);
  assert(labelFactPick(&edge, 1, mkRoom(247, 300, 151)) == -1);
  assert(labelFactPick(&edge, 1, mkRoom(248, 301, 149)) == 0);

  // Nothing to choose from, or only what n counts: the search stops at n.
  assert(labelFactPick(lines, 0, mkRoom(247, 300, 0)) == -1);
  assert(labelFactPick(lines, 3, mkRoom(247, 300, 0)) == -1);   // the 28 px is the fourth
  assert(labelFactPick(lines, 4, mkRoom(247, 300, 0)) == 3);
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "label_fact_size.cpp"), "-o", str(binary)],
                   check=True)
    subprocess.run([str(binary)], check=True)
print("test_label_fact_size: ok")
