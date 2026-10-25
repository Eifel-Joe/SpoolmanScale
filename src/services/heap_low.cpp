#include "heap_low.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <stdio.h>

// The task that started the running measurement, nullptr when none runs.
// A plain "running" flag was rejected: the monitor is global and its start
// fails for the second caller, and that caller's heapLowEnd() would then see
// the flag of the first one, stop its measurement and return its number as its
// own. With the owner, only the task that got the start can end it.
static volatile TaskHandle_t s_owner = nullptr;

bool heapLowBegin() {
  // ESP_FAIL: a measurement is running already, nobody but its owner may end it.
  if (heap_caps_monitor_local_minimum_free_size_start() != ESP_OK) return false;
  s_owner = xTaskGetCurrentTaskHandle();
  return true;
}

uint32_t heapLowEnd() {
  if (s_owner != xTaskGetCurrentTaskHandle()) return 0;
  // Read before the stop: after it the call returns the boot-wide value again.
  const uint32_t low = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  // Given up before the monitor is stopped. The other way round, a task could
  // start a new measurement between the stop and this line and lose it here.
  s_owner = nullptr;
  heap_caps_monitor_local_minimum_free_size_stop();
  return low;
}

const char* heapLowText(uint32_t low, char* buf, size_t len) {
  if (!buf || len == 0) return "";
  if (low == 0) snprintf(buf, len, "n/a");
  else          snprintf(buf, len, "%u", (unsigned)low);
  return buf;
}
