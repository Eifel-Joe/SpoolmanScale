#pragma once

// ============================================================
//  THE FILAMENT DATABASE INDEX, KEPT IN FLASH
//
//  The index (filament_db.h) is boiled down from the whole database: 3 MB
//  read for about 20 kB of makers and materials, 11.5 s on the scale
//  (08.10.2026). In PSRAM it is lost with every restart, and a restart is
//  every update. So the last one is kept in the data partition, behind the
//  Bambu catalog, for a week and for the server it came from.
//
//  A week, because the index only says which makers and materials exist.
//  Spoolman takes SpoolmanDB in once an hour, but it changes when somebody
//  adds to it on GitHub, a few times a month; and the lists themselves are
//  always read fresh, so a new colour shows at once. Only a new maker or a
//  new material of one waits for the week to end.
//
//  The database task never touches the flash (V64): its stack may lie in
//  PSRAM, and a flash access with such a stack in use stops the device
//  (ESP-IDF 5.5, components/spi_flash/cache_utils.c, the assert in
//  spi_flash_disable_interrupts_caches_and_other_cpu()). The loop reads the
//  stored index before the task starts, the task puts it in place or hands
//  a new one over, and a store task with its stack in internal RAM writes
//  it, the way the Bambu catalog does since V59.
// ============================================================

// Loop task, before the index load's task starts. Reads the stored index,
// when there is one younger than a week from this server and the clock is
// set, in small pieces through the loop's own stack into PSRAM, for
// fdbStoreLoad() on the task. Says nothing in the log: the task's load says
// whether it was used, and why not. False when there is none to hand over.
bool fdbStorePrepare(const char* base_url);

// Database task. Puts the index fdbStorePrepare() read in place. Makers come
// back unmarked: whose spools the inventory has is asked again. False when
// there is none. Touches no flash.
bool fdbStoreLoad(const char* base_url);

// Database task. Hands a copy of the index just loaded to the store task
// (fdbStoreTick()). Touches no flash. A failure only costs the next restart
// its 3 MB.
void fdbStoreSave(const char* base_url);

// Loop task, every pass. Starts the store task for a copy that was handed
// over, once the internal heap has the reserve of a background request and
// the store's stack free, and frees the copy when it is written or given up.
void fdbStoreTick();

// Loop task, acceptance build (POST /api/sim/fdb-forget): the next
// fdbStorePrepare() passes the stored index over once, so the whole database
// is read and stored again.
void fdbStoreForgetOnce();
// Loop task: whether the last fdbStorePrepare() passed the stored index over
// for fdbStoreForgetOnce(). A start that is refused after it gives the forget
// back, so the next one still reads the whole database.
bool fdbStorePassedOver();
