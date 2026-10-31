"""Compile the production backend job rules and check how much free heap the list worker needs for each kind of backend address, how far the floor drops for a worker whose stack lies in PSRAM, and that a refused start is logged once per interval with the rest counted."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/backend_job_rules.h"
#include <cassert>

// The two floors, spelled out here on purpose: the production defines must
// agree with this text, not with themselves. 50000 is for https. TLS lives in
// PSRAM in this firmware (platformio.ini), and on the Waveshare 7B on
// 24.10.2026 a GitHub request showed no internal heap use of its own. What a
// background request takes is its stack and its connection, ~22-24 kB with
// or without encryption, so a start at 50 kB leaves ~28 kB, as low as the 7B
// ran without a fault. 57 064 bytes is what the 7B has free in operation
// (25.10.2026), so it clears this floor. 40000 is for plain http: the 16 kB
// stack and the sockets.
static_assert(BACKEND_JOB_MIN_HEAP_TLS == 50000,
              "https: stack and socket like http, TLS lives in PSRAM; 50000 is what the 7B measurements cover");
static_assert(BACKEND_JOB_MIN_HEAP_PLAIN == 40000, "http needs less");

int main() {
  // https, in any case of letters: the TLS floor.
  assert(backendJobMinHeap("https://x.example") == 50000u);
  assert(backendJobMinHeap("HTTPS://X") == 50000u);
  assert(backendJobMinHeap("HtTpS://mixed.example:8443") == 50000u);

  // http: the plain floor. This is the address that locked the 7B out, a
  // Bambuddy on the home network with 58 620 bytes free on the board.
  assert(backendJobMinHeap("http://192.168.10.219:8000") == 40000u);
  assert(backendJobMinHeap("HTTP://host") == 40000u);
  assert(58620u >= backendJobMinHeap("http://192.168.10.219:8000"));
  // And the 7B, which has 57 064 bytes free in operation (25.10.2026), now
  // clears the https floor too: the daily update check and the TLS warm-up
  // start on it.
  assert(57064u >= backendJobMinHeap("https://x.example"));

  // No address, or none given: nothing says it is plain, so the dearer floor.
  assert(backendJobMinHeap("") == 50000u);
  assert(backendJobMinHeap(nullptr) == 50000u);

  // An address without a scheme is http, as backendComposeBase reads it.
  assert(backendJobMinHeap("192.168.1.5:7912") == 40000u);

  // A worker that may talk to either of two addresses needs the higher floor:
  // a Bambuddy over http in front of a Spoolman over https is the case. The
  // pair counts the same either way round, and an address that says nothing
  // is TLS on whichever side it stands.
  assert(backendJobMinHeapBoth("http://192.168.10.219:8000", "http://192.168.10.178:7912") == 40000u);
  assert(backendJobMinHeapBoth("http://192.168.10.219:8000", "https://spoolman.example") == 50000u);
  assert(backendJobMinHeapBoth("https://bambuddy.example", "http://192.168.10.178:7912") == 50000u);
  assert(backendJobMinHeapBoth("https://bambuddy.example", "https://spoolman.example") == 50000u);
  assert(backendJobMinHeapBoth("http://192.168.10.219:8000", "") == 50000u);
  assert(backendJobMinHeapBoth("http://192.168.10.219:8000", nullptr) == 50000u);
  assert(backendJobMinHeapBoth(nullptr, "http://192.168.10.178:7912") == 50000u);
  assert(backendJobMinHeapBoth(nullptr, nullptr) == 50000u);

  // "https" must be followed by "://": a host that merely starts with the
  // letters is not a TLS address.
  assert(backendJobMinHeap("httpsproxy.local:8000") == 40000u);
  assert(backendJobMinHeap("https:/x") == 40000u);

  // A worker whose stack lies in PSRAM takes none of it from the internal
  // heap, so its floor drops by exactly the stack: what the request leaves
  // free while it runs stays what it was with the stack inside. 16384 is the
  // web job, 14336 the daily update check.
  assert(workerStartFloor(50000, 16384, true) == 33616u);
  assert(workerStartFloor(50000, 14336, true) == 35664u);
  assert(workerStartFloor(40000, 16384, true) == 23616u);
  // With the stack inside, the floor is the floor.
  assert(workerStartFloor(50000, 16384, false) == 50000u);
  // A stack larger than the floor leaves no floor, it does not wrap round.
  assert(workerStartFloor(10000, 16384, true) == 0u);

  // What a start may try. The 7B in operation has 46 to 50 kB free: the web
  // job to GitHub starts only with its stack outside.
  WorkerStackPlan p = workerStackPlan(46000, 50000, 16384, true);
  assert(p.outside && !p.inside);
  // 49888, the daily check that beta.8 skipped (log 30.10.2026): it runs now.
  p = workerStackPlan(49888, 50000, 14336, true);
  assert(p.outside && !p.inside);
  // A job that may not move its stack (it reads or writes the flash on its
  // own task) keeps the whole floor: refused at 46 kB as before.
  p = workerStackPlan(46000, 50000, 16384, false);
  assert(!p.outside && !p.inside);
  // Enough for both: outside first, inside if PSRAM gives no stack.
  p = workerStackPlan(60000, 50000, 16384, true);
  assert(p.outside && p.inside);
  p = workerStackPlan(60000, 50000, 16384, false);
  assert(!p.outside && p.inside);
  // The edges: one byte under the floor less the stack is a refusal, the
  // floor less the stack itself is a start.
  p = workerStackPlan(33615, 50000, 16384, true);
  assert(!p.outside && !p.inside);
  p = workerStackPlan(33616, 50000, 16384, true);
  assert(p.outside && !p.inside);
  p = workerStackPlan(50000, 50000, 16384, false);
  assert(!p.outside && p.inside);

  // The log gate. The first call is always due; after that one per interval,
  // and every call that is not due is counted.
  LogGate g{};
  assert(logGateDue(&g, 1000, 60000));
  assert(g.suppressed == 0u);
  assert(!logGateDue(&g, 2000, 60000));
  assert(g.suppressed == 1u);
  assert(!logGateDue(&g, 30000, 60000));
  assert(g.suppressed == 2u);
  // 61000 - 1000 = 60000 ms since the line: exactly one interval is due.
  assert(logGateDue(&g, 61000, 60000));
  // Taking the count returns it and clears it; the next take is 0.
  assert(logGateTakeSuppressed(&g) == 2u);
  assert(logGateTakeSuppressed(&g) == 0u);
  // And the clock restarted at the line: 59999 ms later is still too early.
  assert(!logGateDue(&g, 120999, 60000));
  assert(logGateDue(&g, 121000, 60000));

  // millis() wraps after 49.7 days. From 0xFFFFF000 to 0x00010000 are 69632
  // ms, more than the interval, though the later value is the smaller one.
  LogGate h{};
  assert(logGateDue(&h, 0xFFFFF000u, 60000));
  assert(logGateDue(&h, 0x00010000u, 60000));
  // And a call 1 s after the wrap is not due.
  assert(!logGateDue(&h, 0x00010000u + 1000u, 60000));

  // A reset makes the next call due again, whatever the clock says.
  logGateReset(&h);
  assert(logGateDue(&h, 5, 60000));

  // A reset also forgets the count: the refusals of an episode that ended
  // with a start do not belong in the first line of the next one.
  LogGate k{};
  assert(logGateDue(&k, 1000, 60000));
  assert(!logGateDue(&k, 2000, 60000));
  assert(k.suppressed == 1u);
  logGateReset(&k);
  assert(logGateTakeSuppressed(&k) == 0u);
  assert(logGateDue(&k, 3000, 60000));

  // No gate at all: the line is written (a refusal is better logged than
  // swallowed), nothing is counted, nothing crashes.
  assert(logGateDue(nullptr, 0, 60000));
  assert(logGateTakeSuppressed(nullptr) == 0u);
  logGateReset(nullptr);
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "backend_job_rules.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("backend job rules: ok")
