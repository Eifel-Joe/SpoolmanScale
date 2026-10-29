#pragma once

#include <lvgl.h>

// ============================================================
//  NEW SPOOL FROM A TAG: THE CARD
//
//  One card for all three backends. It opens at once, says what the tag is
//  (colour, product, article, weight) and looks the filament up on the next
//  loop pass: there, or created along with the spool, or not possible. The
//  answer row creates; the result goes to the main screen the way a copy
//  does (finishCopyFlow). The work is in services/backend_api.h.
// ============================================================

// Whether the tag that was scanned last can become a new spool: one an input
// file of services/tag_create.cpp recognises (an NTAG carries no material,
// and every backend needs one) and a backend with a way to do it.
bool tagCreateOffered();

// The tap on the copy popup's tile: raises newtag_open_pending; the loop
// closes the popup and opens the card.
void tagCreateEntryTap(lv_event_t* e);

// Builds the card for the tag that was scanned last. Loop task only. A Bambu
// tag with a roll on the pad, behind Bambuddy (with or without Spoolman), asks
// the spool type first (V33): the card follows the answer, "without type"
// included, and Back closes both, nothing made.
void showTagCreatePopup();

// The same card for a filament picked from the backend's database
// (db_pick_screen.h) rather than read off a tag. The tag on the pad is
// linked to the new spool all the same. Loop task only.
struct TagCreateInput;
void showTagCreatePopupFor(const TagCreateInput& in);

// The lookup and the creation, one loop pass after the card or its button
// asked for them, so the card is drawn before the loop stands still.
void tagCreatePopupTick();

void closeTagCreatePopup();

// The card's screen while it is open, for hiding it with the link overlays.
lv_obj_t* tagCreatePopupScreen();

// The request reached the server but its answer did not come back, so the
// server may have done it: a read timeout, a dropped link, a proxy that gave
// up waiting. A refused connection or a 502 never got that far. Also for the
// run of several spools (batch_create.h).
bool tagCreateAnswerLost(int code);

// A create from the card that made no spool, with the code and the result of
// backendCreateFromTag(): the card says why and stays. After a lost answer the
// button stays off (the spool may exist); a filament made on the way is kept
// for the next try. Also the run's end when it made nothing.
struct TagCreateResult;
void tagCreateFailed(int code, const TagCreateResult& r);
