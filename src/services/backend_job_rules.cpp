#include "services/backend_job_rules.h"

// Plain ASCII, no locale: a URL scheme is ASCII and <ctype.h> would depend on
// the C locale of whatever the unit is compiled for.
static char lowerAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

// True if s starts with prefix, which is given in lower case. At the end of s
// the terminator is not equal to any character of the prefix, so the loop
// stops there and never reads past it.
static bool startsWithNoCase(const char* s, const char* prefix) {
  for (; *prefix; s++, prefix++) {
    if (lowerAscii(*s) != *prefix) return false;
  }
  return true;
}

uint32_t backendJobMinHeap(const char* base_url) {
  // Nothing to read a scheme from: the dearer floor, not the cheaper one.
  if (!base_url || !base_url[0]) return (uint32_t)BACKEND_JOB_MIN_HEAP_TLS;
  return startsWithNoCase(base_url, "https://") ? (uint32_t)BACKEND_JOB_MIN_HEAP_TLS
                                                : (uint32_t)BACKEND_JOB_MIN_HEAP_PLAIN;
}

uint32_t backendJobMinHeapBoth(const char* first, const char* second) {
  const uint32_t a = backendJobMinHeap(first);
  const uint32_t b = backendJobMinHeap(second);
  return a > b ? a : b;
}

uint32_t workerStartFloor(uint32_t min_heap, uint32_t stack_bytes, bool stack_outside) {
  if (!stack_outside) return min_heap;
  return min_heap > stack_bytes ? min_heap - stack_bytes : 0;
}

WorkerStackPlan workerStackPlan(uint32_t free_heap, uint32_t min_heap, uint32_t stack_bytes,
                                bool may_go_outside) {
  WorkerStackPlan p;
  p.outside = may_go_outside && free_heap >= workerStartFloor(min_heap, stack_bytes, true);
  p.inside  = free_heap >= min_heap;
  return p;
}

bool logGateDue(LogGate* g, uint32_t now_ms, uint32_t interval_ms) {
  if (!g) return true;
  // The difference is taken in uint32_t on purpose: it stays right when
  // now_ms has wrapped past last_ms.
  if (!g->armed || (uint32_t)(now_ms - g->last_ms) >= interval_ms) {
    g->armed   = true;
    g->last_ms = now_ms;
    return true;
  }
  g->suppressed++;
  return false;
}

uint32_t logGateTakeSuppressed(LogGate* g) {
  if (!g) return 0;
  const uint32_t n = g->suppressed;
  g->suppressed = 0;
  return n;
}

void logGateReset(LogGate* g) {
  if (!g) return;
  g->armed      = false;
  g->last_ms    = 0;
  g->suppressed = 0;
}
