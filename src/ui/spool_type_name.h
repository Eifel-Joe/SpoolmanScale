#pragma once

// ============================================================
//  A NEW EMPTY SPOOL TYPE, NAMED AT THE SCALE (design A3)
//
//  A full screen with a text field and LVGL's own keyboard. The weight is
//  given by whoever opens it (a weighed empty spool, or one computed from a
//  full spool); the user types the name. Its check mark parks the name, and
//  the loop then loads the catalog fresh, refuses a name that is taken (in
//  any case, against that fresh list: there is no overwriting at the scale,
//  design note L15) and adds the type to Bambuddy's catalog with the weight
//  in whole grams. The keyboard's own keyboard key (LVGL's CANCEL) cancels.
//
//  Bambuddy in Spoolman mode only (spoolTypesActive()). Nothing here talks
//  to the server from a button; spoolTypeNameTick() does, from the loop.
// ============================================================

// Called once the server took the type: the weight as it was stored (whole
// grams) and the name as it was sent. The name entry is closed by then.
typedef void (*SpoolTypeCreated)(float grams, const char* name);

// Opens the name entry, the field filled with prefill (may be "" or null).
// grams outside spoolTypeTarePlausible(), or no Bambuddy in Spoolman mode:
// logged, nothing opens, and on_cancel is called at once, so the caller's
// screen comes back the same way as after a cancel. on_cancel may be null.
void showSpoolTypeName(const char* prefill, float grams, SpoolTypeCreated on_created, void (*on_cancel)());
bool isSpoolTypeNameOpen();
// Closed without calling either callback: for a navigation away, and for the
// screen it was opened from when that one closes.
void closeSpoolTypeName();
// Every loop pass (from tareEntryTick()). Does nothing until a name is
// parked; then the fresh list, the duplicate check and the POST.
void spoolTypeNameTick();
