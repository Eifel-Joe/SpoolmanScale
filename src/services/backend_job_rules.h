#pragma once

#include <stdint.h>

// ============================================================
//  WHEN THE LIST WORKER MAY START: THE RULES
//
//  The list worker (backend_job.cpp) runs the one long backend request, the
//  whole inventory, on a task of its own. The stack of that task comes out
//  of the internal heap the moment it is created, so the start is refused
//  while the heap is below a floor. It used to be one floor for everybody,
//  60000 bytes, written for https: a TLS handshake runs on that task
//  whenever the kept connection is closed, and the stack of 16 kB is sized
//  for it.
//
//  That floor locked the Waveshare 7B out. The board has about 58 620 bytes
//  free in steady state, 1 380 bytes under it, and stays there: the heap
//  never recovers to 60000, so a Bambuddy over plain http (the usual setup
//  at home, http://192.168.x.x:8000) was never asked about an unknown tag.
//  Each refusal also wrote a log line, over 4000 of them, and the lookup
//  gave up after 120 s with "spool not found". Plain http needs no
//  handshake: the 16 kB stack, a socket and the buffers of one request, so
//  its floor is lower. What a run really takes is in the log line the job
//  writes when it is done (free at the start, lowest since boot).
//
//  Only the address decides: it is the one thing known before the task
//  exists. The update check (update_check.cpp) keeps its own floor of 60000
//  on purpose, it always speaks https to GitHub.
//
//  The rules live here, without Arduino, the heap or the log: a host test
//  holds them to the values in test_backend_job_rules.py, and
//  backend_job.cpp asks this unit instead of keeping rules of its own.
// ============================================================

#define BACKEND_JOB_MIN_HEAP_TLS   60000   // https: the TLS handshake runs on the worker
#define BACKEND_JOB_MIN_HEAP_PLAIN 40000   // http: the 16 kB stack plus plain sockets

// The free internal heap the list worker needs before it may start, for a
// backend at this base URL. "https://" in any case of letters, an empty
// address or none: the TLS floor, because nothing says it is plain. Anything
// else is http, a bare address without scheme included, which is how the
// backend layer reads it (backendComposeBase).
uint32_t backendJobMinHeap(const char* base_url);

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
