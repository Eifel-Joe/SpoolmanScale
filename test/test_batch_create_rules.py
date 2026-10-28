"""Compile the production rules for creating several spools at once (n10, V44) and check them: a count is a whole number from 1 to 10 as the number pad hands it over; after each spool the run goes on, is done, or stops at a failure or at an answer it cannot be sure of; the spool ids made are written as "41, 43-45", a run of three or more as a range (services/batch_create_rules.h)."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/batch_create_rules.h"
#include <cassert>
#include <cmath>
#include <cstring>

// The limits, spelled out here on purpose: the production defines must agree
// with this text, not with themselves. One to ten spools, and the text of
// their ids in 95 bytes and a terminator.
static_assert(BATCH_COUNT_MIN == 1, "one spool at least");
static_assert(BATCH_COUNT_MAX == 10, "ten spools at most");
static_assert(BATCH_IDS_TEXT_MAX == 96, "95 bytes and the terminator");
// The steps as the plan numbers them.
static_assert(BS_NEXT == 0 && BS_DONE == 1 && BS_STOP_FAILED == 2 && BS_STOP_UNSURE == 3, "the steps");

int main() {
  // The values of the plan (n10, Task 6).
  assert(batchCountValid(1) && batchCountValid(10) && batchCountValid(3.0f));
  assert(!batchCountValid(0) && !batchCountValid(11) && !batchCountValid(2.5f) && !batchCountValid(NAN));
  assert(batchCreateStep(1, 5, true, false) == BS_NEXT && batchCreateStep(5, 5, true, false) == BS_DONE);
  assert(batchCreateStep(2, 5, false, false) == BS_STOP_FAILED);
  assert(batchCreateStep(2, 5, false, true) == BS_STOP_UNSURE);
  assert(batchCreateStep(1, 1, true, false) == BS_DONE);
  char t[BATCH_IDS_TEXT_MAX]; const int a[] = {41, 42, 43, 44, 45}, b[] = {41, 43, 44, 45}, c[] = {41, 42};
  assert(batchIdsText(a, 5, t, sizeof t) && strcmp(t, "41-45") == 0);
  assert(batchIdsText(b, 4, t, sizeof t) && strcmp(t, "41, 43-45") == 0);
  assert(batchIdsText(c, 2, t, sizeof t) && strcmp(t, "41, 42") == 0);
  const int ten[] = {10001, 10003, 10005, 10007, 10009, 10011, 10013, 10015, 10017, 10019};
  assert(batchIdsText(ten, 10, t, sizeof t));                 // the widest case fits
  assert(!batchIdsText(a, 0, t, sizeof t) && !batchIdsText(a, 5, t, 4) && t[0] == 0);

  // Counts: every whole number from 1 to 10, nothing else, however close.
  for (int n = 1; n <= 10; n++) assert(batchCountValid((float)n));
  assert(!batchCountValid(-1) && !batchCountValid(0.5f) && !batchCountValid(9.5f) && !batchCountValid(10.5f));
  assert(!batchCountValid(INFINITY) && !batchCountValid(-INFINITY));

  // The run: on while spools are wanted, done with the last, a stop at the
  // first that fails; the made ones stay made.
  assert(batchCreateStep(4, 5, true, false) == BS_NEXT);
  assert(batchCreateStep(9, 10, true, false) == BS_NEXT && batchCreateStep(10, 10, true, false) == BS_DONE);
  assert(batchCreateStep(0, 5, false, false) == BS_STOP_FAILED);   // the first one failed
  assert(batchCreateStep(0, 5, false, true) == BS_STOP_UNSURE);
  assert(batchCreateStep(0, 1, false, false) == BS_STOP_FAILED);

  // The widest text: ten ids of five digits, none next to another, is
  // "10001, 10003, ..., 10019", 10 * 5 + 9 * 2 = 68 bytes.
  assert(batchIdsText(ten, 10, t, sizeof t) && strlen(t) == 68);
  assert(strncmp(t, "10001, 10003, ", 14) == 0 && strcmp(t + 61, ", 10019") == 0);
  // One id, a run of exactly three, a run and two loose ones after it.
  const int one[] = {7}, three[] = {1, 2, 3}, mixed[] = {1, 2, 3, 5, 6}, two_runs[] = {1, 2, 3, 7, 8, 9, 10};
  assert(batchIdsText(one, 1, t, sizeof t) && strcmp(t, "7") == 0);
  assert(batchIdsText(three, 3, t, sizeof t) && strcmp(t, "1-3") == 0);
  assert(batchIdsText(mixed, 5, t, sizeof t) && strcmp(t, "1-3, 5, 6") == 0);
  assert(batchIdsText(two_runs, 7, t, sizeof t) && strcmp(t, "1-3, 7-10") == 0);
  // The buffer: exactly the text and its terminator fits, one byte less does
  // not, and a refusal leaves it empty.
  assert(batchIdsText(a, 5, t, 6) && strcmp(t, "41-45") == 0);
  strcpy(t, "stale");
  assert(!batchIdsText(a, 5, t, 5) && t[0] == 0);
  strcpy(t, "stale");
  assert(!batchIdsText(a, -1, t, sizeof t) && t[0] == 0);
  strcpy(t, "stale");
  assert(!batchIdsText(b, 4, t, 9) && t[0] == 0);              // "41, 43-45" is 9 bytes
  assert(batchIdsText(b, 4, t, 10) && strcmp(t, "41, 43-45") == 0);
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "batch_create_rules.cpp"), "-o", str(binary)],
                   check=True)
    subprocess.run([str(binary)], check=True)
print("batch create rules: ok")
