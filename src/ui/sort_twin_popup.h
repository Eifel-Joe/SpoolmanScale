#pragma once

#include <lvgl.h>

// ============================================================
//  SAME COLOUR UNDER ANOTHER NAME
//
//  Over the new-spool card, when a sort typed in at the scale has the colour
//  of a filament the inventory already holds from the same maker and
//  material, but under another name (TagFilamentPlan::twin_*). Two answers:
//  take that filament, or create a new one all the same. Built on demand,
//  its buttons only park the answer, sortTwinPopupTick() hands it on and
//  deletes the box from the loop.
// ============================================================

// `name` is the filament as the inventory names it. `done` gets true when
// that filament is to be taken, false for a new one.
void showSortTwinPopup(const char* name, void (*done)(bool take));

bool sortTwinPopupOpen();
void closeSortTwinPopup();

// From the loop: carries out the answer the buttons parked.
void sortTwinPopupTick();
