#include "services/worker_task.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/idf_additions.h>
#include <sdkconfig.h>

// FreeRTOS refuses a stack outside internal RAM unless this is set
// (components/freertos/heap_idf.c, xPortcheckValidStackMem); for the S3 it is
// on by default and platformio.ini sets it under its old name. Where it is
// off, or there is no PSRAM, PSRAM is not even tried.
#if defined(CONFIG_SPIRAM) && defined(CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM)
#define WORKER_STACK_OUTSIDE 1
#else
#define WORKER_STACK_OUTSIDE 0
#endif

// The three boards of this firmware all have PSRAM (platformio.ini). Built
// without the two options the stacks would stay inside without a word, and
// the 7B would refuse the GitHub check again: a build error instead.
#if !WORKER_STACK_OUTSIDE && (defined(BOARD_WT32_SC01_PLUS) || defined(BOARD_WAVESHARE_S3_LCD7) || \
                              defined(BOARD_WAVESHARE_S3_LCD7B))
#error "worker stacks cannot go to PSRAM: CONFIG_SPIRAM or CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM is off"
#endif

// The stacks in PSRAM of the workers that are still there. Changed only by
// workerTaskStart() and workerTaskReap(), both on the loop task.
static volatile uint32_t s_outside_bytes = 0;

uint32_t workerFreeHeap() {
  const uint32_t free_heap = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const uint32_t held      = s_outside_bytes;
  return free_heap > held ? free_heap - held : 0;
}

bool workerStackCanGoOutside(uint32_t stack_bytes) {
#if WORKER_STACK_OUTSIDE
  return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) >= stack_bytes;
#else
  (void)stack_bytes;
  return false;
#endif
}

bool workerTaskStart(WorkerTask* w, const WorkerStackPlan& plan, const WorkerTaskSpec& spec) {
  if (w->handle) return false;
  // Before the task exists: it may be done and parked before the handle
  // below is written, and the flag must not be cleared after that.
  w->parked = false;
  TaskHandle_t h = nullptr;
#if WORKER_STACK_OUTSIDE
  if (plan.outside &&
      xTaskCreatePinnedToCoreWithCaps(spec.fn, spec.name, spec.stack_bytes, nullptr,
                                      spec.priority, &h, spec.core,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
    w->outside       = true;
    w->outside_bytes = spec.stack_bytes;
    w->handle        = h;
    s_outside_bytes  = s_outside_bytes + spec.stack_bytes;
    return true;
  }
#endif
  // The same call with internal caps rather than xTaskCreatePinnedToCore():
  // one way to delete for both, vTaskDeleteWithCaps().
  if (plan.inside &&
      xTaskCreatePinnedToCoreWithCaps(spec.fn, spec.name, spec.stack_bytes, nullptr,
                                      spec.priority, &h, spec.core,
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) == pdPASS) {
    w->outside       = false;
    w->outside_bytes = 0;
    w->handle        = h;
    return true;
  }
  return false;
}

void workerTaskPark(WorkerTask* w) {
  // The task's results first, then the flag: the loop reads the flag and
  // deletes the task, it must not see the flag before what came with it.
  __sync_synchronize();
  w->parked = true;
  // Between the flag and the suspend the task holds no lock, so the loop may
  // suspend it there as well. A suspended task that is resumed by mistake
  // parks again.
  for (;;) vTaskSuspend(NULL);
}

bool workerTaskReap(WorkerTask* w) {
  if (!w->handle) return true;
  if (!w->parked) return false;
  // Suspends the task if it is not yet, waits until no core runs it, deletes
  // it and frees its stack and TCB, all before it returns.
  vTaskDeleteWithCaps(w->handle);
  s_outside_bytes  = s_outside_bytes - w->outside_bytes;
  w->handle        = nullptr;
  w->parked        = false;
  w->outside       = false;
  w->outside_bytes = 0;
  return true;
}
