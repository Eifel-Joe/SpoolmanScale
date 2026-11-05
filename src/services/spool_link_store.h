#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
//  THE SPOOL'S OWN LINK, KEPT FOR THE LABEL (Spec 5.3 U3, V75)
//
//  What a read of a spool on the Spoolman way says about its own
//  page (backendSpoolLink(), o18) is put here by spool id, and
//  every way that draws a spool's label or shows its link asks
//  here. The slots (services/spool_link_slots.h) live in PSRAM,
//  allocated on the first link; any task may put or ask, a lock
//  keeps a read from meeting half a write. Without PSRAM nothing
//  is kept and the code is built from the id, as before o18.
//
//  Wurzel: o18 kept the link in sm_page_url and in SpoolLabelData,
//  three times 128 bytes of internal RAM on the 7B, which has about
//  45 kB left in use (Punkteliste n120). Fix: PSRAM, by id, tied to
//  the server it came from. NOT-TO-DO: a field in SpoolLabelData -
//  two statics and many stack frames hold that struct.
// ============================================================

// What a read of spool `id` from server generation `gen` (backendGeneration()
// when the read started) named: kept when spoolLinkOk() takes it, else what
// was kept for the spool is dropped.
void spoolLinkPut(int id, const char* url, uint32_t gen);
// The current server's link for spool `id` into out (always terminated);
// false and "" when it named none.
bool spoolLinkCopy(int id, char* out, size_t out_size);
// Whether the current server named a link for spool `id`.
bool spoolLinkHas(int id);
