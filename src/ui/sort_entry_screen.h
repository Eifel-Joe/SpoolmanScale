#pragma once

#include <lvgl.h>

// ============================================================
//  A NEW FILAMENT SORT ENTERED AT THE SCALE: THE SCREEN
//
//  For a spool the database has no entry for. A blank NTAG lies on the pad,
//  "New / Copy" offers "Enter filament" (and "New from database" leads to it
//  where its lists end), and short steps collect maker, material, product
//  line, colour and the weight of a full spool, each chosen from a list made
//  of the inventory, the filament database and a few usual values, the
//  keyboard only for what no list has (design note W,
//  docs/plans/2026-10-26-w-sorte-eingeben.md). Then the way of "New from
//  database" takes over: the spool type, the empty spool, the card of a new
//  spool (tag_create_popup.h).
//
//  The lists come from services/sort_entry_source.h, the rules from
//  services/sort_entry_rules.h. Every tap is parked and carried out by
//  sortEntryTick() on the next loop pass: a list is rebuilt there, and the
//  inventory is asked there, behind the waiting card.
// ============================================================

// Whether the copy popup offers the entry for the tag on the pad: a tag that
// is not a Bambu one, and a backend other than FilaMan (nobody can check it
// there yet).
bool sortEntryOffered();

// The tap on the copy popup's tile. Parks the open; sortEntryTick() opens
// the entry at its first step, the maker.
void sortEntryEntryTap(lv_event_t* e);

// The picker's way in, where its maker or material list ends: a tile across
// `width` that opens the entry (at the material step for `maker`, at the
// maker step for nullptr). False, and nothing drawn, when the entry is not
// offered.
bool sortEntryPickTile(lv_obj_t* parent, int width, const char* maker);

// Every loop pass, from the spool flow's deferred actions, before
// dbPickTick(): that one takes any finished database load, the picker closed
// or not, and the entry's loads have to be collected first.
void sortEntryTick();

void sortEntryClose();

// The entry's screen while it is open, for hiding it with the link overlays.
lv_obj_t* sortEntryScreen();
