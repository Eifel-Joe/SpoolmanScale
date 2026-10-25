#include "heap_low.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <stdio.h>

// The task that holds the running measurement, nullptr when none runs. Only
// touched through the atomic builtins: the daily check, the web jobs and the
// scale's own check run on different tasks and cores.
//
// The claim comes before the monitor is started, and that order is the fix.
// The code used to start the monitor first and take the start's result as the
// lock, on the belief that a second start fails while one runs. It does not:
// in ESP-IDF 5.5 (components/heap/heap_caps.c,
// heap_caps_monitor_local_minimum_free_size_start) it returns ESP_OK every
// time and resets the minimum of every heap, so a second caller silently reset
// the first one's measurement and then took the ownership as well. A plain
// "running" flag was rejected for the same reason: test and set are two steps,
// and two tasks could both see it clear.
static TaskHandle_t s_owner = nullptr;

bool heapLowBegin() {
  TaskHandle_t none = nullptr;
  TaskHandle_t self = xTaskGetCurrentTaskHandle();
  // Somebody else's measurement is running: leave it alone, this one has none.
  if (!__atomic_compare_exchange_n(&s_owner, &none, self, false,
                                   __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
    return false;
  }
  // Not with this ESP-IDF, see above; a later one may refuse. The claim goes
  // back then, or no measurement could ever start again. none is still
  // nullptr here: a successful exchange leaves it as it was.
  if (heap_caps_monitor_local_minimum_free_size_start() != ESP_OK) {
    __atomic_store_n(&s_owner, none, __ATOMIC_RELEASE);
    return false;
  }
  return true;
}

uint32_t heapLowEnd() {
  if (__atomic_load_n(&s_owner, __ATOMIC_ACQUIRE) != xTaskGetCurrentTaskHandle()) return 0;
  // Read before the stop: after it the call returns the boot-wide value again.
  const uint32_t low = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  // Stopped before the claim is given back. The other way round, a task could
  // claim and start a measurement of its own between the two lines and have
  // its monitor stopped by this one.
  heap_caps_monitor_local_minimum_free_size_stop();
  TaskHandle_t none = nullptr;
  __atomic_store_n(&s_owner, none, __ATOMIC_RELEASE);
  return low;
}

const char* heapLowText(uint32_t low, char* buf, size_t len) {
  if (!buf || len == 0) return "";
  if (low == 0) snprintf(buf, len, "n/a");
  else          snprintf(buf, len, "%u", (unsigned)low);
  return buf;
}
