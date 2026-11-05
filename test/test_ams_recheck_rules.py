"""Compile the rules of the bay recheck (services/ams_recheck_rules.h, Spec 5.3 AC j07, v0.8.7-beta.4, V71) and check them. BamBuddy releases a bay the AMS reports empty for two minutes, also with Spoolman behind it, and a bay that was empty first and then "occupied but unreadable" counts as a swap (maziggy/bambuddy dev, slot_unlink_grace.py): an assignment made before the spool went in is lost. The scale looks 2.5, 5 and 8 minutes after an assignment and sends it again at most twice, while the AMS reports a spool in the bay and no other spool is on file for it (Jensilein, efb7257b). Ours watches up to eight assignments at once (Jörg weighs several spools before he loads them, E3), a new assignment of the same spool or to the same bay takes the old watch's place, and taking the spool out or "not into the AMS" ends its watch (E5)."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/ams_recheck_rules.h"
#include <cassert>
#include <cstring>

static AmsRecheck t[AMS_RECHECK_SLOTS];

static int activeCount() {
  int n = 0;
  for (int i = 0; i < AMS_RECHECK_SLOTS; i++) n += t[i].active ? 1 : 0;
  return n;
}

int main() {
  static_assert(AMS_RECHECK_SLOTS == 8, "eight watches at once: as many bays as Jarvis has (E3)");
  static_assert(AMS_RECHECK_ROUNDS == 3, "three looks");
  static_assert(AMS_RECHECK_RESEND_MAX == 2, "sent again at most twice");
  assert(AMS_RECHECK_AT_MS[0] == 150000u);   // after BamBuddy's two minutes
  assert(AMS_RECHECK_AT_MS[1] == 300000u);
  assert(AMS_RECHECK_AT_MS[2] == 480000u);

  // --- starting and finding what is due -------------------------------------
  amsRecheckClear(t, AMS_RECHECK_SLOTS);
  assert(activeCount() == 0);
  const uint32_t t0 = 1000;
  const int a = amsRecheckStart(t, AMS_RECHECK_SLOTS, 51, 2, 0, 1, t0);
  assert(a >= 0 && t[a].active && t[a].spool_id == 51 && t[a].printer_id == 2 &&
         t[a].ams_id == 0 && t[a].tray_id == 1 && t[a].round == 0 && t[a].resent == 0);
  assert(amsRecheckDue(t, AMS_RECHECK_SLOTS, t0 + 149999) == -1);
  assert(amsRecheckDue(t, AMS_RECHECK_SLOTS, t0 + 150000) == a);

  // A second spool into another bay is watched beside the first (E3).
  const int b = amsRecheckStart(t, AMS_RECHECK_SLOTS, 52, 2, 0, 2, t0 + 5000);
  assert(b >= 0 && b != a && activeCount() == 2);
  // The same spool assigned again (moved) takes its own watch's place.
  const int a2 = amsRecheckStart(t, AMS_RECHECK_SLOTS, 51, 2, 0, 3, t0 + 6000);
  assert(a2 == a && activeCount() == 2 && t[a].tray_id == 3 && t[a].since_ms == t0 + 6000);
  // Another spool into a watched bay takes that bay's watch.
  const int c = amsRecheckStart(t, AMS_RECHECK_SLOTS, 53, 2, 0, 2, t0 + 7000);
  assert(c == b && activeCount() == 2 && t[b].spool_id == 53);
  // The same bay number on another printer is another bay.
  const int d = amsRecheckStart(t, AMS_RECHECK_SLOTS, 54, 3, 0, 2, t0 + 7000);
  assert(d != b && activeCount() == 3);

  // Full: the ninth replaces the oldest watch.
  amsRecheckClear(t, AMS_RECHECK_SLOTS);
  for (int i = 0; i < AMS_RECHECK_SLOTS; i++) {
    amsRecheckStart(t, AMS_RECHECK_SLOTS, 100 + i, 3, i / 4, i % 4, t0 + (uint32_t)i * 10);
  }
  assert(activeCount() == AMS_RECHECK_SLOTS);
  const int ninth = amsRecheckStart(t, AMS_RECHECK_SLOTS, 200, 1, 0, 0, t0 + 1000);
  assert(ninth >= 0 && t[ninth].spool_id == 200 && activeCount() == AMS_RECHECK_SLOTS);
  for (int i = 0; i < AMS_RECHECK_SLOTS; i++) assert(t[i].spool_id != 100);   // the oldest is gone

  // Stopping by spool (taken out, "not into the AMS"): only that one.
  assert(amsRecheckStop(t, AMS_RECHECK_SLOTS, 103) == 1);
  assert(activeCount() == AMS_RECHECK_SLOTS - 1);
  assert(amsRecheckStop(t, AMS_RECHECK_SLOTS, 103) == 0);
  assert(amsRecheckStop(t, AMS_RECHECK_SLOTS, 999) == 0);
  // A nonsense start is refused.
  assert(amsRecheckStart(t, AMS_RECHECK_SLOTS, 0, 1, 0, 0, t0) == -1);
  assert(amsRecheckStart(t, AMS_RECHECK_SLOTS, 5, 0, 0, 0, t0) == -1);
  assert(amsRecheckStart(t, AMS_RECHECK_SLOTS, 5, 1, -1, 0, t0) == -1);
  assert(amsRecheckStart(t, AMS_RECHECK_SLOTS, 5, 1, 0, -1, t0) == -1);

  // Round by round; the clock may wrap around.
  amsRecheckClear(t, AMS_RECHECK_SLOTS);
  const uint32_t wrap = 0xFFFFFFF0u;
  const int w = amsRecheckStart(t, AMS_RECHECK_SLOTS, 60, 2, 0, 1, wrap);
  assert(amsRecheckDue(t, AMS_RECHECK_SLOTS, wrap + 149999u) == -1);
  assert(amsRecheckDue(t, AMS_RECHECK_SLOTS, wrap + 150000u) == w);
  amsRecheckLooked(t[w]);
  assert(t[w].active && t[w].round == 1);
  assert(amsRecheckDue(t, AMS_RECHECK_SLOTS, wrap + 299999u) == -1);
  assert(amsRecheckDue(t, AMS_RECHECK_SLOTS, wrap + 300000u) == w);
  amsRecheckLooked(t[w]);
  assert(amsRecheckDue(t, AMS_RECHECK_SLOTS, wrap + 480000u) == w);
  amsRecheckLooked(t[w]);
  assert(!t[w].active);                      // ends after the last look
  assert(amsRecheckDue(t, AMS_RECHECK_SLOTS, wrap + 900000u) == -1);

  // --- what one look decides --------------------------------------------------
  AmsRecheck r{};
  r.active = true; r.spool_id = 51; r.printer_id = 2; r.ams_id = 0; r.tray_id = 1;
  // The spool's place could not be asked: look again next round.
  assert(amsRecheckAfterFind(r, -1, -1, -1) == RC_NO_ANSWER);
  assert(amsRecheckAfterFind(r, 503, -1, -1) == RC_NO_ANSWER);
  assert(!amsRecheckStepEnds(RC_NO_ANSWER));
  // Still in the bay: nothing to do, keep watching.
  assert(amsRecheckAfterFind(r, 200, 0, 1) == RC_STILL_THERE);
  assert(!amsRecheckStepEnds(RC_STILL_THERE));
  // Somewhere else now, by a tap here or in BamBuddy: not ours to undo.
  assert(amsRecheckAfterFind(r, 200, 0, 2) == RC_MOVED);
  assert(amsRecheckAfterFind(r, 200, 1, 1) == RC_MOVED);
  assert(amsRecheckStepEnds(RC_MOVED));
  // Gone: ask on.
  assert(amsRecheckAfterFind(r, 200, -1, -1) == RC_GO_ON);
  // Another spool on file for the bay: leave it.
  assert(amsRecheckAfterBay(r, 7) == RC_OTHER_SPOOL);
  assert(amsRecheckStepEnds(RC_OTHER_SPOOL));
  assert(amsRecheckAfterBay(r, 0) == RC_GO_ON);
  assert(amsRecheckAfterBay(r, -1) == RC_GO_ON);
  assert(amsRecheckAfterBay(r, 51) == RC_GO_ON);  // itself: the lookup raced the drop
  // The AMS: an empty or unreadable answer sends nothing.
  assert(amsRecheckAfterPresence(r, true, false) == RC_BAY_EMPTY);
  assert(amsRecheckAfterPresence(r, false, false) == RC_BAY_UNKNOWN);
  assert(amsRecheckAfterPresence(r, false, true) == RC_BAY_UNKNOWN);
  assert(!amsRecheckStepEnds(RC_BAY_EMPTY) && !amsRecheckStepEnds(RC_BAY_UNKNOWN));
  // A spool in the bay: sent again, twice at most, then given up.
  assert(amsRecheckAfterPresence(r, true, true) == RC_RESEND);
  assert(!amsRecheckStepEnds(RC_RESEND));
  r.resent = 1;
  assert(amsRecheckAfterPresence(r, true, true) == RC_RESEND);
  r.resent = 2;
  assert(amsRecheckAfterPresence(r, true, true) == RC_GIVE_UP);
  assert(amsRecheckStepEnds(RC_GIVE_UP));
  assert(!amsRecheckStepEnds(RC_GO_ON));
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "ams_recheck_rules.cpp"), "-o", str(binary)],
                   check=True)
    subprocess.run([str(binary)], check=True)
print("test_ams_recheck_rules: ok")
