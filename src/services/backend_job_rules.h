#pragma once

#include <stdint.h>

// ============================================================
//  WHEN A BACKEND WORKER MAY START: THE RULES
//
//  A background task that talks to a backend runs off the loop on a task of
//  its own: the list worker (backend_job.cpp) the one long request, the whole
//  inventory, the others a request or a few. The stack of such a task comes
//  out of the internal heap the moment it is created, so the start is refused
//  while the heap is below a floor. It used to be one floor for everybody,
//  60000 bytes, written for https: a TLS handshake runs on that task
//  whenever the kept connection is closed, and the handshake was taken to
//  need 40 kB on top of the stack.
//
//  That floor locked the Waveshare 7B out. The board has about 58 620 bytes
//  free in steady state, 1 380 bytes under it, and stays there: the heap
//  never recovers to 60000, so a Bambuddy over plain http (the usual setup
//  at home, http://192.168.x.x:8000) was never asked about an unknown tag,
//  and no other worker that talks to it started either. Each refusal also
//  wrote a log line, over 4000 of them, and the lookup gave up after 120 s
//  with "spool not found". Plain http needs no handshake: the 16 kB stack,
//  a socket and the buffers of one request, so its floor is lower. What a
//  run really takes is in the log line the list job writes when it is done
//  (free at the start, lowest since boot).
//
//  The 40 kB for the handshake do not hold either, and that is why https is
//  50000 and not 60000. The figure comes from a board where mbedTLS took its
//  memory from the internal heap. Here it does not: TLS lives in PSRAM
//  (platformio.ini, CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC), and on the 7B on
//  24.10.2026 seven requests to GitHub showed no internal heap use of their
//  own, more was free while the connection was open, not less. What a
//  background request takes is its stack and its connection, some 22 to
//  24 kB, encrypted or not. A start at 50000 leaves about 28 kB while it
//  runs, as low as the 7B has run without a fault (25.10.2026); its 57 kB
//  in operation clear the floor. 40000 for https would be a guess, nothing
//  has been measured that low, and the 10 kB over the plain floor stand for
//  what has not been: the low point inside the handshake itself.
//
//  Only the address decides: it is the one thing known before the task
//  exists. A worker asks for the floor of the address it is about to talk
//  to, the higher one if it talks to more than one. The daily update check
//  (update_check.cpp) asks for the address of its version file, always
//  https to GitHub, and so does the TLS warm-up in backend_http.cpp, which
//  only runs for an https backend and asks for the backend's address like
//  the list worker. The floor is meant for one background request, and a
//  second stack from the same heap would take what it leaves, so the daily
//  check does not start while a web job or the inventory list runs, and the
//  GitHub jobs of the web pages do not start while the daily check or the
//  inventory list runs.
//
//  A refused start is asked for again on every pass of the loop, and so is a
//  task that could not be created, so each of those two log lines goes
//  through a LogGate of its own, at most once per BACKEND_JOB_REFUSAL_LOG_MS.
//
//  The rules live here, without Arduino, the heap or the log: a host test
//  holds them to the values in test_backend_job_rules.py, and the workers
//  ask this unit instead of keeping rules of their own
//  (test_worker_heap_floors.py looks that none keeps a number, the update
//  check and the TLS warm-up included).
// ============================================================

#define BACKEND_JOB_MIN_HEAP_TLS   50000   // https: stack and socket like http, TLS lives in PSRAM
#define BACKEND_JOB_MIN_HEAP_PLAIN 40000   // http: the 16 kB stack plus plain sockets

// A refused start is logged at most this often (see LogGate below), with the
// number of refusals the line stands for. One value for every worker.
#define BACKEND_JOB_REFUSAL_LOG_MS 60000

// The free internal heap a backend worker needs before it may start, for a
// backend at this base URL. "https://" in any case of letters, an empty
// address or none: the TLS floor, because nothing says it is plain. Anything
// else is http, a bare address without scheme included, which is how the
// backend layer reads it (backendComposeBase).
uint32_t backendJobMinHeap(const char* base_url);

// The higher of the two floors, for a worker that may talk to either of two
// addresses: a BamBuddy that keeps its inventory in a Spoolman server may
// speak http while that Spoolman speaks https (or the other way round), and a
// request of the worker may go to either. An address that is empty or missing
// counts as TLS on whichever side it stands, as above.
uint32_t backendJobMinHeapBoth(const char* first, const char* second);

// Says whether a line that can repeat on every pass may be written now:
// the first time, then once per interval. The calls that are refused are
// counted, so that the line can say how many it stands for. Zero-initialise
// it (LogGate g{}); one gate for one line, used from one task.
struct LogGate {
  bool     armed;       // a line has been written, last_ms is valid
  uint32_t last_ms;     // when it was written
  uint32_t suppressed;  // calls refused since then
};

// True if the line is due: the first call, and then once interval_ms has
// passed since the last line, counted modulo 2^32 so that a millis() that
// wraps does not stall the line for 49 days. Every other call counts as
// suppressed. A due call does not touch the count, the caller takes it with
// logGateTakeSuppressed() for the line it is about to write. No gate: due.
bool logGateDue(LogGate* g, uint32_t now_ms, uint32_t interval_ms);

// The calls refused since the last line, which it also clears. 0 for no gate.
uint32_t logGateTakeSuppressed(LogGate* g);

// Makes the next call due again and forgets the count: for when what was
// refused has stopped, so that the next episode starts with a line of its own.
void logGateReset(LogGate* g);
