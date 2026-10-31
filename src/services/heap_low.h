#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
//  HOW LOW THE INTERNAL HEAP WENT DURING ONE REQUEST
//
//  "Lowest since boot" (heap_caps_get_minimum_free_size) cannot say that. It
//  only moves when a new record is set, so a request that went deep, but not
//  deeper than the boot had gone, reads the same before and after: the list
//  job's line shows "lowest since boot 28572 (was 28572)" whatever the list
//  cost. For the update path the number that matters is the low point of
//  that one request, because the start floor (backend_job_rules.h) is set
//  from it: a request that starts at 50 kB must stay above what the board
//  has run at without a fault.
//
//  ESP-IDF has a monitor for exactly this: from heapLowBegin() on, the
//  minimum is counted from the moment of the call instead of from boot, and
//  heapLowEnd() reads it and hands the boot-wide value back.
//
//  Three things to know:
//   - The monitor is global, there is one for the whole device. While a
//     measurement runs, heap_caps_get_minimum_free_size() shows the local
//     value to everybody: the "lowest since boot" of the list job
//     (backend_job.cpp) and heap_min of the perf line (perf_monitor.cpp,
//     ESP.getMinFreeHeap()). The daily check and the GitHub jobs of the web
//     pages do not start beside the list, but the scale's own check and
//     download do not wait for it, and the perf line is written whenever its
//     window ends. What either reads in the window is the local value,
//     which is never lower than the true one.
//   - It sees the whole internal heap, not this request's allocations: what
//     the other tasks take in the same window counts. That is what the floor
//     is about, the lowest the device went while the request ran.
//   - The monitor does not refuse a second start. In ESP-IDF 5.5
//     (components/heap/heap_caps.c,
//     heap_caps_monitor_local_minimum_free_size_start) it returns ESP_OK
//     every time and resets the minimum of every heap, which would quietly
//     restart a measurement that is running. So there is one owner, claimed
//     atomically before the monitor is started: a task that does not get
//     the claim has no measurement, and its heapLowEnd() returns 0 and
//     touches nothing. One measurement at a time device wide, not nested.
//
//  Every way out of a request has to end the measurement, the failing ones
//  too. A measurement left running keeps the claim: every later start is
//  refused, and every later line reads n/a.
// ============================================================

// Starts the measurement on the calling task. False when another task holds
// it, or when the monitor did not start: this call then has no value to give.
bool heapLowBegin();

// The lowest free internal heap since heapLowBegin(), in bytes, and the end
// of the measurement. 0 when this task has no measurement running: its
// heapLowBegin() failed or was not called, or heapLowEnd() already ran. Safe
// to call on any path, it stops the monitor only for the task that started it.
uint32_t heapLowEnd();

// A low point for a log line: the number, or "n/a" for 0. Written into buf
// (12 bytes hold any value) and returned, so it can stand in the argument
// list of the log call.
const char* heapLowText(uint32_t low, char* buf, size_t len);
