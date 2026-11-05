"""Compile the spacing of the reports to BamBuddy (services/bambuddy_device_rules.h, Spec 5.3 AC a03, v0.8.7-beta.4, V71) and check it. The heartbeat (every 10 s) and the live weight (at most once a second) go out from a task of their own, with timeouts of 3 s and 1.5 s (AreYouThereAndi2, 7abb09a). A server that does not answer is asked less often: the heartbeat's gap doubles up to 30 s and is back at 10 s with the first answer (a 404 is an answer: the device was removed, the scale registers again); the weight is spaced by what the last report cost, ten times its time, never closer than a second, and a failure doubles the gap up to 30 s."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/bambuddy_device_rules.h"
#include <cassert>

int main() {
  static_assert(BB_HEARTBEAT_MS == 10000, "BamBuddy's own daemon beats every 10 s");
  static_assert(BB_WEIGHT_MIN_GAP_MS == 1000, "the weight at most once a second");
  static_assert(BB_HEARTBEAT_TIMEOUT_MS == 3000, "Andi: 3 s for the heartbeat");
  static_assert(BB_WEIGHT_TIMEOUT_MS == 1500, "Andi: 1.5 s for the weight");
  static_assert(BB_BACKOFF_MAX_MS == 30000, "never further apart than 30 s");
  static_assert(BB_REPORT_COST_FACTOR == 10, "a report may take a tenth of the time");

  // --- heartbeat --------------------------------------------------------------
  assert(bbHeartbeatGapMs(200, BB_HEARTBEAT_MS) == 10000u);
  assert(bbHeartbeatGapMs(-11, BB_HEARTBEAT_MS) == 20000u);   // no answer: twice the gap
  assert(bbHeartbeatGapMs(-1, 20000) == 30000u);              // and the cap
  assert(bbHeartbeatGapMs(503, 30000) == 30000u);
  assert(bbHeartbeatGapMs(200, 30000) == 10000u);             // the first answer: back
  assert(bbHeartbeatGapMs(404, 30000) == 10000u);             // removed: an answer too
  assert(bbHeartbeatGapMs(-11, 0) == 20000u);                 // never below the plain rate

  // --- weight -----------------------------------------------------------------
  assert(bbWeightGapMs(200, 30, BB_WEIGHT_MIN_GAP_MS) == 1000u);   // fast server: once a second
  assert(bbWeightGapMs(200, 150, 1000) == 1500u);                  // ten times what it cost
  assert(bbWeightGapMs(200, 5000, 1000) == 30000u);                // capped
  assert(bbWeightGapMs(-11, 1500, 1000) == 2000u);                 // failure: twice the gap
  assert(bbWeightGapMs(-11, 1500, 2000) == 4000u);
  assert(bbWeightGapMs(-11, 1500, 16000) == 30000u);
  assert(bbWeightGapMs(-1, 0, 30000) == 30000u);
  assert(bbWeightGapMs(200, 40, 30000) == 1000u);                  // an answer: back to the cost
  assert(bbWeightGapMs(-11, 10, 0) == 1000u);                      // never closer than a second
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "bambuddy_device_rules.cpp"), "-o", str(binary)],
                   check=True)
    subprocess.run([str(binary)], check=True)
print("test_bambuddy_report_rules: ok")
