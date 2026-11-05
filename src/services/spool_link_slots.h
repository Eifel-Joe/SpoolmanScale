#pragma once

#include <stdint.h>
#include <string.h>

#include "services/spool_link_rules.h"

// ============================================================
//  THE SLOTS A SPOOL'S OWN LINK IS KEPT IN (Spec 5.3 U3, V75)
//
//  A server on the Spoolman way may name a page for each spool
//  (o18, the original's beta.53). The original keeps it next to
//  the scan, in each label copy and in the copy for a failed
//  server label: three times 128 bytes of internal RAM, where the
//  7B has about 45 kB left in use. Here a few slots hold the
//  links by spool id and by the server they came from, and the
//  slots live in PSRAM (services/spool_link_store.cpp).
//
//  Pure logic, no Arduino and no locking: the store adds both.
//  test_spool_link_store.py holds the rules.
// ============================================================

// Enough for the spool on the pad, a run of labels (up to 10, V44, printed
// one after the other, each read just before) and a few more.
#define SPOOL_LINK_SLOTS 8

struct SpoolLinkSlot {
  int      id;    // the spool's; 0 for a free slot
  uint32_t gen;   // backendGeneration() of the read that named it
  char     url[SPOOL_LINK_LEN];
};

// The slots and where the next one is taken when none is free.
struct SpoolLinkSlots {
  SpoolLinkSlot* slot;
  int n;
  int next;
};

// The slot that holds spool `id`'s link from server generation `gen`, -1
// for none. A slot of another generation is another server's: never found.
inline int spoolLinkSlotFind(const SpoolLinkSlots& s, int id, uint32_t gen) {
  if (!s.slot || id <= 0) return -1;
  for (int i = 0; i < s.n; i++)
    if (s.slot[i].id == id && s.slot[i].gen == gen) return i;
  return -1;
}

// What one read of spool `id` from generation `gen` said. A link that
// spoolLinkOk() takes is kept: in the slot that holds this spool, else in a
// free one (a slot of another generation counts as free), else in the one
// s.next points at, which then moves on, so the oldest goes first. Anything
// else - no link, an empty one, one the rules refuse - drops what was kept
// for this spool: the server stopped naming it, and an old link would print.
// True when a link is kept now.
inline bool spoolLinkSlotPut(SpoolLinkSlots& s, int id, uint32_t gen, const char* url) {
  if (!s.slot || s.n <= 0 || id <= 0) return false;
  int at = spoolLinkSlotFind(s, id, gen);
  if (!spoolLinkOk(url, SPOOL_LINK_LEN - 1)) {
    if (at >= 0) s.slot[at].id = 0;
    return false;
  }
  if (at < 0) {
    for (int i = 0; i < s.n && at < 0; i++)
      if (s.slot[i].id == 0 || s.slot[i].gen != gen) at = i;
  }
  if (at < 0) {
    at = s.next >= 0 && s.next < s.n ? s.next : 0;
    s.next = (at + 1) % s.n;
  }
  s.slot[at].id = id;
  s.slot[at].gen = gen;
  memcpy(s.slot[at].url, url, strlen(url) + 1);
  return true;
}
