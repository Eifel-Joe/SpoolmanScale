#pragma once

#include <lvgl.h>
#include <stddef.h>

// ============================================================
//  SEVERAL IDENTICAL SPOOLS: THE COUNT ON THE CARDS
//
//  n10 (V44, "docs/entwurf/2026-10-28-beta6-bedienung.md", part 4): a create
//  without a tag (X, noTagMode()) can make several spools that are alike. Both
//  cards that create one, the copy confirmation and the card for a new spool
//  (from the database, or a sort typed in), get a small button at their top
//  right, "Anzahl 1"; a tap opens the price pad for a whole number from
//  BATCH_COUNT_MIN to BATCH_COUNT_MAX (services/batch_create_rules.h), and the
//  create button then reads "3 anlegen". The count lasts while the flag does:
//  every call of noTagModeSet() puts it back to one.
//
//  Wurzel (the place): the design note has a line "Anzahl: 1" on the card
//  (Festlegung 2), and neither card has one free. The copy card behind
//  Bambuddy on Spoolman is 304 of the screen's 320 px high already
//  (COPY_CARD_BBS_GROW in copy_confirm_popup.cpp).
//  Fix: a button in the corner above the title, where both cards have nothing
//  but the icon in the middle.
//  NOT-TO-DO: growing the cards by a line: behind Bambuddy on Spoolman the copy
//  card would be taller than the screen.
//  See test_batch_flow.py (the card).
// ============================================================

// How many spools the create is to make: 1 until a count is typed in.
int batchCount();

// Back to one. noTagModeSet() calls it whenever the flag is set, so that a
// count never reaches another create.
void batchCountReset();

// Builds the count button on a create card; box is the card's box, UI_POPUP_W
// wide. It reads "Anzahl 1", or the count typed in. A tap opens the price pad
// with the count filled in. A valid answer is the count from then on, and
// `changed` is called for the card to show it: the copy card builds itself
// again, the card for a new spool puts the text on its create button. An
// answer that is no whole number from BATCH_COUNT_MIN to BATCH_COUNT_MAX
// changes nothing, and the button shows the range in place of the count. An
// answer for a card that was closed meanwhile, or after the flag went off, is
// dropped. The cards build it only in the flag.
void batchCountChip(lv_obj_t* box, void (*changed)());

// The text of the create button, written into buf (cap bytes; 32 hold it in
// every language) and returned: "Anlegen" for one spool, "3 anlegen" for three.
const char* batchCreateCaption(char* buf, size_t cap);
