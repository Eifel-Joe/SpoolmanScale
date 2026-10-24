#pragma once

#include <stdint.h>

// ============================================================
//  PANEL SCALE: 480 x 320 PICTURE ON THE 7B's 1024 x 600 GLASS
//
//  The interface stays 480 x 320 and is enlarged onto the whole glass,
//  nearest neighbour: 32/15 across (1024 / 480) and 15/8 down (600 / 320).
//  Both ratios are exact, so the picture ends on the last glass pixel with
//  no remainder. One picture pixel becomes 2 or 3 glass pixels across and
//  1 or 2 down, which is why the blocks are not all the same size.
//
//  LVGL does not hand over the whole picture but strips of it, of any size,
//  and the panel has nothing but the pixels of the strip in front of it. So
//  two things must hold for every strip [a, b) of the picture:
//    - the glass pixels the strip covers are exactly those whose source
//      pixel lies inside [a, b), so no glass pixel reads outside the strip;
//    - the next strip starts on the very glass pixel this one stops at, so
//      nothing stays black and nothing is drawn twice.
//
//  The one rule that gives both: a glass pixel o shows picture pixel
//  floor(o / ratio) (scaleSource), and a strip starts at the first glass
//  pixel that shows a picture pixel of the strip, ceil(a * ratio)
//  (scaleSpan). Rounding the strip's edges any other way, floor or nearest,
//  puts the first or last glass pixel on a source pixel just outside the
//  strip. test/test_panel_scale.py runs every strip up to 40 pixels through
//  both functions and fails on exactly that.
//
//  Whole numbers only, no float: coordinates stay in the thousands, so
//  coordinate * ratio numerator is far from overflowing an int32_t.
// ============================================================

// Half open range of glass pixels along one axis: start is the first, end is
// one past the last. An empty strip gives start == end.
struct ScaleSpan {
  int32_t start;
  int32_t end;
};

// The glass pixels covered by the picture pixels [in_start, in_end), for an
// enlargement of num/den. Both bounds are picture coordinates, 0 <= in_start
// <= in_end. The result is [ceil(in_start * num / den), ceil(in_end * num / den)).
// Called once per axis, with (32, 15) across and (15, 8) down.
ScaleSpan scaleSpan(int32_t in_start, int32_t in_end, int32_t num, int32_t den);

// The picture pixel shown by glass pixel `out` (0 <= out), floor(out * den /
// num). Inside any span from scaleSpan it lands inside the strip that span
// was made for.
int32_t scaleSource(int32_t out, int32_t num, int32_t den);
