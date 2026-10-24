"""Compile the production 7B enlargement maths and check that the strips cover the glass without gaps or overlap."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "boards/waveshare_s3_lcd7b/panel_scale.h"
#include <cassert>
#include <stdint.h>

// One axis of the enlargement. The numbers are spelled out here on purpose:
// the production maths must agree with this text, not with itself.
struct Axis {
  int32_t num, den;     // output = input * num / den
  int32_t n_in, n_out;  // the 480 x 320 picture and the 1024 x 600 glass
  int32_t min_px, max_px;  // output pixels one input pixel may become
};

int main() {
  const Axis axes[] = {
    { 32, 15, 480, 1024, 2, 3 },  // across: 2.13 pixels per pixel
    { 15,  8, 320,  600, 1, 2 },  // down:   1.88 pixels per pixel
  };

  for (const Axis& ax : axes) {
    // The whole picture is the whole glass, edge to edge.
    ScaleSpan whole = scaleSpan(0, ax.n_in, ax.num, ax.den);
    assert(whole.start == 0 && whole.end == ax.n_out);

    // LVGL hands in strips of any size. Wherever the picture is cut in two,
    // the first part ends where the second begins: nothing left black,
    // nothing drawn twice.
    for (int32_t s = 0; s <= ax.n_in; s++) {
      ScaleSpan head = scaleSpan(0, s, ax.num, ax.den);
      ScaleSpan tail = scaleSpan(s, ax.n_in, ax.num, ax.den);
      assert(head.start == 0);
      assert(head.end == tail.start);
      assert(tail.end == ax.n_out);
    }

    // One input pixel is a block of two or three output pixels across, one or
    // two down. Together they add up to the glass.
    int32_t total = 0;
    for (int32_t x = 0; x < ax.n_in; x++) {
      ScaleSpan one = scaleSpan(x, x + 1, ax.num, ax.den);
      int32_t width = one.end - one.start;
      assert(width >= ax.min_px && width <= ax.max_px);
      total += width;
    }
    assert(total == ax.n_out);

    // The ends of the glass read the ends of the picture.
    assert(scaleSource(0, ax.num, ax.den) == 0);
    assert(scaleSource(ax.n_out - 1, ax.num, ax.den) == ax.n_in - 1);

    // A strip [a, b): every output pixel in its span reads from inside the
    // strip, because panelWrite only has the strip's own pixels. The pixel
    // just before and the one just after read from outside it, so the span
    // is neither too small nor too big.
    for (int32_t a = 0; a < ax.n_in; a++) {
      for (int32_t b = a + 1; b <= a + 40 && b <= ax.n_in; b++) {
        ScaleSpan sp = scaleSpan(a, b, ax.num, ax.den);
        assert(sp.start < sp.end);
        for (int32_t o = sp.start; o < sp.end; o++) {
          int32_t src = scaleSource(o, ax.num, ax.den);
          assert(src >= a && src < b);
        }
        if (sp.start > 0) assert(scaleSource(sp.start - 1, ax.num, ax.den) < a);
        if (sp.end < ax.n_out) assert(scaleSource(sp.end, ax.num, ax.den) >= b);
      }
    }
  }

  // Spot values worked out by hand. With 32/15, pixel 0 is the glass pixels
  // 0..2 and pixel 1 is 3..4 (ceil(32/15) = 3, ceil(64/15) = 5). With 15/8,
  // pixel 0 is 0..1 and pixel 1 is 2..3 (ceil(15/8) = 2, ceil(30/8) = 4).
  // A span is half open, so these read {0, 3}, {3, 5}, {0, 2} and {2, 4}.
  ScaleSpan p0 = scaleSpan(0, 1, 32, 15);
  ScaleSpan p1 = scaleSpan(1, 2, 32, 15);
  assert(p0.start == 0 && p0.end == 3);
  assert(p1.start == 3 && p1.end == 5);
  ScaleSpan q0 = scaleSpan(0, 1, 15, 8);
  ScaleSpan q1 = scaleSpan(1, 2, 15, 8);
  assert(q0.start == 0 && q0.end == 2);
  assert(q1.start == 2 && q1.end == 4);
  assert(scaleSource(1023, 32, 15) == 479);
  assert(scaleSource(599, 15, 8) == 319);

  // An empty strip is an empty span, wherever it sits.
  ScaleSpan none = scaleSpan(200, 200, 32, 15);
  assert(none.start == none.end);
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "boards" / "waveshare_s3_lcd7b" / "panel_scale.cpp"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("panel scale: ok")
