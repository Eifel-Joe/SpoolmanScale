#include "panel_scale.h"

// ceil(a / b) for a >= 0 and b > 0. C++ division truncates, which for a
// non-negative a is floor, so adding b - 1 first rounds up instead.
static int32_t ceilDiv(int32_t a, int32_t b) {
  return (a + b - 1) / b;
}

ScaleSpan scaleSpan(int32_t in_start, int32_t in_end, int32_t num, int32_t den) {
  // Glass pixel o shows picture pixel floor(o * den / num). That is at least
  // in_start exactly when o * den / num >= in_start, that is when o >=
  // in_start * num / den, so the first such o is the rounded-up quotient. The
  // same argument with "< in_end" gives the end, which is why the range is
  // half open. Each edge is computed on its own from its own picture
  // coordinate, so the end of one strip and the start of the next are the
  // same number whenever the strips touch.
  return { ceilDiv(in_start * num, den), ceilDiv(in_end * num, den) };
}

int32_t scaleSource(int32_t out, int32_t num, int32_t den) {
  return (out * den) / num;
}
