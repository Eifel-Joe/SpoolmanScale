#pragma once

#include <Arduino.h>

#include "services/backend_job_rules.h"

// ============================================================
//  A WORKER TASK WITH ITS STACK IN PSRAM
//
//  The web job and the daily update check used to take their 14-16 kB stack
//  from the internal heap, and their floor counted it: on the 7B, with 46 to
//  50 kB free in operation, the GitHub check of the firmware page answered
//  "busy" and the daily check was skipped (31.10.2026). Their stack now
//  comes from PSRAM where the board has it, and the floor drops by the stack
//  (workerStackPlan() in backend_job_rules.h). Without PSRAM, or when PSRAM
//  gives no block, the stack comes from the internal heap as before.
//
//  A stack in PSRAM must never be in use while the cache is off. A flash
//  access turns it off on a board that runs its code from flash: ESP-IDF
//  then asserts that the task doing it has its stack in internal RAM
//  (components/spi_flash/cache_utils.c,
//  spi_flash_disable_interrupts_caches_and_other_cpu()), and the WT32 would
//  restart. So only a task that does not touch the flash itself may have
//  one: no NVS, no Update, no esp_partition_*. Its log lines are fine,
//  logSD() hands them to the loop task (sd_logger.cpp). Another task's
//  flash write is fine too: the other core is parked in an IPC task with an
//  internal stack meanwhile, and this one is not running.
//
//  The task does not delete itself. A task whose stack came from heap_caps
//  goes with vTaskDeleteWithCaps(), and called on itself that makes a helper
//  task from the internal heap and aborts when it cannot
//  (components/freertos/esp_additions/idf_additions.c): a restart where a
//  vTaskDelete(NULL) used to cost nothing. So the task parks itself as its
//  last step, and the loop deletes it (workerTaskReap), the way ESP-IDF
//  recommends there.
// ============================================================

struct WorkerTask {
  TaskHandle_t  handle;         // the task, until the loop has deleted it
  volatile bool parked;         // the task is done and waits in workerTaskPark()
  bool          outside;        // its stack lies in PSRAM
  uint32_t      outside_bytes;  // that stack's size, counted by workerFreeHeap()
};

// The free internal heap every worker tests its floor against, in place of
// ESP.getFreeHeap(): that number less the stacks in PSRAM of the workers that
// are still there (started and not yet deleted by the loop).
//
// Root: a floor was set for one background request beside the rest of the
// firmware, and on the 7B it kept the others out while a GitHub request ran,
// because that request's stack lowered the free heap. Moved to PSRAM, the
// stack no longer does: beside the daily check a tag probe would see 14 kB
// more and start, its own stack and connection on top (review, 31.10.2026).
// Counted here, every other worker sees the heap as it did with the stack
// inside, on every board, and the request itself still starts lower.
// Not done instead: making each worker wait for the GitHub requests. That
// would also hold them up on boards with heap for both, which run them side
// by side today.
// The "postponed, heap X < Y" lines of the workers show this number, so
// beside a GitHub request X is lower than the heap by that request's stack.
// See test_worker_stack.py (3a).
uint32_t workerFreeHeap();

// Whether a stack of this size may be tried in PSRAM here: a firmware built
// with PSRAM and with FreeRTOS taking a stack from outside internal RAM
// (CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM), and a block that large free.
bool workerStackCanGoOutside(uint32_t stack_bytes);

// The task to create: what xTaskCreatePinnedToCore() is told.
struct WorkerTaskSpec {
  TaskFunction_t fn;
  const char*    name;
  uint32_t       stack_bytes;
  UBaseType_t    priority;
  BaseType_t     core;
};

// Loop task. Creates the task by the plan: its stack from PSRAM when
// plan.outside, else, or when that fails, from the internal heap when
// plan.inside. False when nothing was created, also while w still holds the
// last task (workerTaskReap() first).
bool workerTaskStart(WorkerTask* w, const WorkerStackPlan& plan, const WorkerTaskSpec& spec);

// The task's last call, in place of vTaskDelete(NULL): says it is done and
// suspends itself. Everything it wrote before is visible to the loop by then.
// Never returns, so what the task function declares is never destroyed: a
// task keeps its clients and documents in a function that returns first.
[[noreturn]] void workerTaskPark(WorkerTask* w);

// Loop task. Deletes a parked task and gives its stack and its TCB back.
// True when w holds no task afterwards, false while the task still runs.
bool workerTaskReap(WorkerTask* w);
