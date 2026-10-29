#include "services/batch_create_rules.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

// The least ids in a row that are written as a range: "41-43" is shorter than
// "41, 42, 43", "41, 42" is not shorter than "41-42".
#define BATCH_RANGE_MIN_RUN 3

bool batchCountValid(float typed) {
  // Both comparisons are false for a value that is not a number, and the bounds
  // keep the cast below inside int, so no infinity gets that far.
  if (!(typed >= (float)BATCH_COUNT_MIN && typed <= (float)BATCH_COUNT_MAX)) return false;
  return typed == (float)(int)typed;
}

BatchStep batchCreateStep(int made, int wanted, bool ok, bool unsure) {
  if (!ok) return unsure ? BS_STOP_UNSURE : BS_STOP_FAILED;
  return made >= wanted ? BS_DONE : BS_NEXT;
}

// The text being built in the caller's buffer. Once something does not fit,
// `ok` is false for good and nothing more is written, so the caller can add
// pieces without a check after each and look at `ok` once at the end.
struct IdsSink {
  char* out;
  size_t cap;
  size_t used;   // bytes in `out` before the terminator
  bool ok;
};

// Adds text; fails, and keeps failing, when it and the terminator do not fit.
static void putText(IdsSink& sink, const char* text) {
  size_t len = strlen(text);
  if (!sink.ok || sink.used + len + 1 > sink.cap) {
    sink.ok = false;
    return;
  }
  memcpy(sink.out + sink.used, text, len + 1);
  sink.used += len;
}

static void putNumber(IdsSink& sink, int value) {
  char digits[16];   // "-2147483648" is 11 bytes and the terminator
  snprintf(digits, sizeof digits, "%d", value);
  putText(sink, digits);
}

bool batchIdsText(const int* ids, int n, char* out, size_t cap) {
  // With no room for even the terminator there is nothing to leave empty.
  if (!out || cap == 0) return false;
  out[0] = 0;
  if (!ids || n < 1) return false;

  IdsSink sink = {out, cap, 0, true};
  int i = 0;
  while (i < n) {
    // `last` ends the run that begins at i: each id one above the one before.
    // The INT_MAX test keeps the +1 from overflowing.
    int last = i;
    while (last + 1 < n && ids[last] != INT_MAX && ids[last + 1] == ids[last] + 1) last++;

    if (i > 0) putText(sink, ", ");
    if (last - i + 1 >= BATCH_RANGE_MIN_RUN) {
      putNumber(sink, ids[i]);
      putText(sink, "-");
      putNumber(sink, ids[last]);
      i = last + 1;
    } else {
      // One or two in a row: the first alone. For two, the second is looked at
      // in the next round and stands alone as well, since no run of three
      // starts there.
      putNumber(sink, ids[i]);
      i++;
    }
  }

  if (!sink.ok) {
    out[0] = 0;
    return false;
  }
  return true;
}
