"""Compile the rules of the documentation build (services/doc_build_rules.h) and check them: a touch from the web interface (a tap, a long press or a swipe, in the coordinates of the 480x320 interface, refused outside it), where the finger is at a moment of a swipe and when it lifts, the raw reading that makes the scale show a simulated weight, and that this build only reads from a server: GET and HEAD pass, every writing method is refused. This build exists for the screenshots of the documentation only and is never released."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/doc_build_rules.h"
#include <cassert>
#include <cmath>

int main() {
  // --- a touch: inside the interface, with a duration in range ---------------
  static_assert(DOC_UI_W == 480 && DOC_UI_H == 320, "the coordinates of the screenshots");
  DocTouch t;
  // A request as the route builds it. A helper, because the commas of a braced
  // list inside assert() would split the macro's argument.
  auto mk = [&t](int x, int y, int ms, int x2, int y2) {
    const DocTouchAsk ask = {x, y, ms, x2, y2};
    return docTouchMake(ask, t);
  };
  // A tap with the default duration (ms 0 means: not given).
  assert(mk(240, 292, 0, -1, -1));
  assert(t.x == 240 && t.y == 292 && t.x2 == 240 && t.y2 == 292 && t.ms == DOC_TOUCH_TAP_MS);
  static_assert(DOC_TOUCH_TAP_MS == 80, "a tap is 80 ms");
  // A long press.
  assert(mk(10, 20, 1200, -1, -1) && t.ms == 1200);
  // A swipe from (100, 250) to (100, 50) in 400 ms.
  assert(mk(100, 250, 400, 100, 50));
  assert(t.x2 == 100 && t.y2 == 50 && t.ms == 400);
  // The corners are inside, one pixel further is not.
  assert(mk(0, 0, 0, -1, -1));
  assert(mk(479, 319, 0, -1, -1));
  assert(!mk(480, 10, 0, -1, -1));
  assert(!mk(10, 320, 0, -1, -1));
  assert(!mk(-1, 10, 0, -1, -1));
  // The end of a swipe must be inside as well; only one end coordinate is no swipe.
  assert(!mk(10, 10, 300, 480, 10));
  assert(!mk(10, 10, 300, 100, -1));
  // The duration: 20 ms to 3000 ms.
  static_assert(DOC_TOUCH_MIN_MS == 20 && DOC_TOUCH_MAX_MS == 3000, "20 ms to 3 s");
  assert(mk(10, 10, 20, -1, -1));
  assert(mk(10, 10, 3000, -1, -1));
  assert(!mk(10, 10, 19, -1, -1));
  assert(!mk(10, 10, 3001, -1, -1));
  assert(!mk(10, 10, -5, -1, -1));

  // --- where the finger is ----------------------------------------------------
  int16_t x = -1, y = -1;
  assert(mk(100, 250, 400, 100, 50));
  assert(docTouchAt(t, 0, x, y) && x == 100 && y == 250);      // down at the start
  assert(docTouchAt(t, 200, x, y) && x == 100 && y == 150);    // half way
  assert(docTouchAt(t, 399, x, y) && x == 100 && y == 50);     // the last poll still pressed, at the end
  assert(!docTouchAt(t, 400, x, y));                            // lifted once the time is up
  assert(!docTouchAt(t, 5000, x, y));
  // A tap stays where it is.
  assert(mk(240, 292, 0, -1, -1));
  assert(docTouchAt(t, 0, x, y) && x == 240 && y == 292);
  assert(docTouchAt(t, 79, x, y) && x == 240 && y == 292);
  assert(!docTouchAt(t, 80, x, y));
  // A swipe to the left and up: the steps go the other way.
  assert(mk(400, 300, 100, 0, 0));
  assert(docTouchAt(t, 50, x, y) && x == 200 && y == 150);

  // --- the simulated weight ---------------------------------------------------
  // The loop turns a reading into grams as (raw - zero_offset) / cal_factor;
  // the simulated reading must come out as the grams asked for.
  const int32_t zero = 81234;
  const float factor = 107.25f;
  const float asked[] = {0.0f, 1.0f, 245.0f, 1234.5f, 3000.0f, -3.0f};
  for (float g : asked) {
    const int32_t raw = docSimRaw(g, zero, factor);
    const float back = (float)(raw - zero) / factor;
    assert(std::fabs(back - g) < 0.01f);
  }
  // A scale that is not calibrated (factor 0 or not a number) shows nothing: the zero reading.
  assert(docSimRaw(500.0f, zero, 0.0f) == zero);
  assert(docSimRaw(500.0f, zero, NAN) == zero);
  // The argument of the web request: grams from -100 to 10000, or "off".
  float g = -1.0f;
  bool off = false;
  assert(docSimParse("1234.5", g, off) && !off && std::fabs(g - 1234.5f) < 0.001f);
  assert(docSimParse("0", g, off) && !off && g == 0.0f);
  assert(docSimParse("-100", g, off) && !off && g == -100.0f);
  assert(docSimParse("10000", g, off) && !off && g == 10000.0f);
  assert(docSimParse("off", g, off) && off);
  assert(docSimParse("OFF", g, off) && off);
  assert(!docSimParse("10001", g, off));
  assert(!docSimParse("-101", g, off));
  assert(!docSimParse("12abc", g, off));
  assert(!docSimParse("", g, off));
  assert(!docSimParse(nullptr, g, off));
  assert(!docSimParse("nan", g, off));

  // --- only reading from a server ---------------------------------------------
  assert(docWriteAllowed("GET") && docWriteAllowed("get") && docWriteAllowed("HEAD"));
  assert(!docWriteAllowed("POST") && !docWriteAllowed("PATCH") && !docWriteAllowed("PUT"));
  assert(!docWriteAllowed("DELETE") && !docWriteAllowed("delete"));
  assert(!docWriteAllowed("") && !docWriteAllowed(nullptr));
  assert(!docWriteAllowed("GETX"));
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "doc_build_rules.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("doc build rules: ok")
