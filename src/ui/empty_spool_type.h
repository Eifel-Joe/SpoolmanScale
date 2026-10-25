#pragma once

// ============================================================
//  AN EMPTY SPOOL WITHOUT A DATABASE ENTRY, ADDED AS A SPOOL TYPE (design B)
//
//  Settings -> Scale -> "Add empty spool". The empty spool lies on the pad
//  and the screen shows its weight live, without the parts ticked here (a
//  clip, an RFID holder, as set up in the web settings). The ticks start at
//  the default and hold for this screen only: there is no spool they could
//  be remembered for, so they are never stored (design note L16). Next takes
//  the weight at the press and asks for the name (spool_type_name.h); once
//  Bambuddy's catalog has the type, the screen says so and stays open for
//  the next spool.
//
//  Bambuddy in Spoolman mode and a load cell only; the menu row asks both.
//  It belongs to no spool, so a spool swap does not close it, but it counts
//  in isTareEntryOpen(): automatic weighing and the pad's drift correction
//  wait while it stands.
// ============================================================

void showEmptySpoolType();
bool isEmptySpoolTypeOpen();
// Closed, not hidden, without a callback; a name entry opened from it goes
// with it.
void closeEmptySpoolType();
// Every loop pass (from tareEntryTick()): the live weight and the Next button.
void emptySpoolTypeTick();
