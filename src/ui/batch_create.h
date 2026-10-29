#pragma once

#include <lvgl.h>
#include <stddef.h>

#include "services/label_layout.h"   // LabelKind

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
// with an empty field. A valid answer is the count from then on, and
// `changed` is called for the card to show it: the copy card builds itself
// again, the card for a new spool puts the text on its create button. An
// emptied field keeps the count. Anything else typed that is no whole number
// from BATCH_COUNT_MIN to BATCH_COUNT_MAX changes nothing, and the button
// shows the range in place of the count. An answer for a card that was closed
// meanwhile, or after the flag went off, is dropped. The cards build it only
// in the flag.
void batchCountChip(lv_obj_t* box, void (*changed)());

// The text of the create button, written into buf (cap bytes; 32 hold it in
// every language) and returned: "Anlegen" for one spool, "3 anlegen" for three.
const char* batchCreateCaption(char* buf, size_t cap);

// ============================================================
//  THE RUN: THE SPOOLS ONE AFTER ANOTHER
//
//  With a count above one, and only in the flag, both creates hand their
//  values over to a run here instead of making one spool: the copy
//  (doCopySpoolCreate()) and the card for a new spool (runCreate() in
//  tag_create_popup.cpp). A count of one never gets here and takes today's
//  way unchanged. The run makes batchCount() spools, one call to the server
//  each, with "Spool 2 of 5 ..." on the loading overlay before each call. It
//  holds the loop for every call as one create does (HttpStallTime), and
//  there is no cancel half way through.
//
//  Wurzel: one call per spool, and any of them can fail. Going on after a
//  failure would leave holes the user cannot see, and a try after a lost
//  answer may have made the spool already.
//  Fix: after each try batchCreateStep() (services/batch_create_rules.h)
//  says what follows; the run goes on only on BS_NEXT and otherwise stops,
//  the spools made stay made. With at least one made, noTagBatchFinish()
//  takes over with their ids and, after a stop, its reason ("Spool 4: ...").
//  With none made, the run ends as one create that failed ends today: the
//  copy with its red line, the card with its own line, where a filament made
//  on the way is kept for the next try.
//  NOT-TO-DO: the first spool through today's way and only the rest here:
//  the first create would show no progress and end a failure in its own
//  words, so the progress and the stop rule would live in two places.
//  See test_batch_flow.py (the run).
// ============================================================

struct SpoolStart;
struct TagCreateInput;
struct TagFilamentPlan;
struct TagSpoolValues;

// The copy, batchCount() times with the same values, each through
// backendCreateSpool() as doCopySpoolCreate() makes one. Behind Bambuddy on
// Spoolman the template the card loaded (and a price typed on it) serves
// every copy and is freed once, after the run; what each copy left behind
// (copyBbsLastMissing()) is gathered and said once at the end, as for one.
void batchCopyCreate(int template_spool_id, int template_filament_id, const SpoolStart& start,
                     float tare_g, float chosen_core_g);

// The card's spool, batchCount() times through backendCreateFromTag() with the
// same values. plan is the card's: once a spool has made its filament the plan
// is that filament, so every further spool hangs on it. vendor is the maker
// for the finish. A price the server did not keep is said once at the end.
void batchTagCreate(const TagCreateInput& in, TagFilamentPlan* plan, const TagSpoolValues& values,
                    const char* vendor);

// ============================================================
//  AFTER THE RUN: ONE OVERVIEW, ONE LOCATION, ALL LABELS
//
//  noTagBatchFinish() (no_tag_create.h) shows the first spool as after one
//  create and keeps the run here. What follows asks once for all of them:
//  the overview with every number, the location picker for the first, its
//  choice written to the others (batchLocationToRest()), then "5 labels?"
//  and every label one after another (batchLabels*()). Joerg: all labels at
//  the end in one go, not each one started by hand at the scale.
// ============================================================

// Keeps the numbers of the spools a run made, in the order made (n of them,
// at most BATCH_COUNT_MAX), until the next run. A run is known by its first
// spool, which is the one on the screen after it.
void batchRunKeep(const int* ids, int n);

// How many spools the kept run has when first_id is its first, else 0; with
// ids set, *ids points at the kept numbers (valid until the next keep).
int batchRunOf(int first_id, const int** ids);

// Writes the location the picker wrote to the run's first spool to each of
// the others, one call each with "Location 2 of 5 ..." on the loading
// overlay; name nullptr clears it, as "no location" did for the first. Stops
// at the first write that fails and says which spools were left without,
// in yellow. From the loop: it holds it for each call.
void batchLocationToRest(int first_id, const char* name);

// "Print" for a run with the preview on (labelPrinterAskFirst()): the sheet
// that comes next is the first spool's, and its print goes on with the
// others (batchLabelsStart(kind, 1)). The sheet closed without its print
// drops them (batchLabelsForget(), or the tick sees it gone).
void batchLabelsAfterSheet(int first_id);

// The labels of the run whose first spool is on the screen, from place
// `from` (0: all of them, 1: after the sheet printed the first) with the
// layout `kind`. Takes the first spool's label data now (its location is the
// one just chosen); each label changes only the number. From the sheet it
// starts only when the sheet was the run's (batchLabelsAfterSheet()) and its
// own label went out. Only flags: the prints run from batchLabelsTick().
void batchLabelsStart(LabelKind kind, int from);

// From the loop, once a pass: the next label once the print card of the one
// before has closed; a result other than printed or sent, or a printer's reply
// to the label before, stops the rest, whose numbers go to the status line in
// yellow. The run is over once its last label's card has closed.
void batchLabelsTick();

// Drops the labels still to come (the sheet closed without its print).
void batchLabelsForget();

// Whether labels of a run are being printed (for noTagAfterCreateBusy()).
bool batchLabelsBusy();
