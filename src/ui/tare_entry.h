#pragma once

// The empty spool weight, weighed or typed in (issue #40). Opened from the
// weight popup's empty spool button. Either way the value goes on to the
// popup that asks where it is stored - spool, filament or brand.
void showTareChoice();

// The first weighing (Bambuddy in Spoolman mode, design 3): a spool with a
// tag that lies still on the pad is asked once per session which empty spool
// it sits on, with the list of spool types (spool_type_list.h) as
// spoolTypeFirstAsk() decides. A pick is stored as the spool's empty weight;
// "later" holds until the next restart. Opened from tareEntryTick().

// Both dialogs of this file, for the navigation, and with them the empty
// spool screen of the settings, the list of spool types and a name entry for
// a new spool type (empty_spool_type.h, spool_type_list.h, spool_type_name.h).
// Closed, not hidden, without their callbacks.
void closeTareEntry();

// The choice, the keypad, the list of the first weighing or the empty spool
// screen is up. Counts as a modal like the weight popup: no automatic weighing
// and no question on removal while it stands.
bool isTareEntryOpen();

// Automatic weighing waits for the first weighing: true while the spool on
// the pad may still be asked (it passes the first weighing's own conditions,
// a dialog standing or not, and was not looked at yet), and from the moment
// its question opened until its tag is gone or another spool is loaded. The
// rest stays as it was after an answer, and "later" leaves rest and tare
// untouched, so the question comes again after a restart.
bool tareEntryHoldsAutoWeigh();

// Every loop pass. Runs the ticks of the list of spool types, the name entry
// and the empty spool screen and opens the first weighing, then closes the
// choice, the keypad or the list of the first weighing without saving once
// the spool they were opened for is lifted off the pad or another spool is
// loaded. The empty spool screen belongs to no spool and stays.
void tareEntryTick();

// Forgets which spools the first weighing has looked at, for a backend switch:
// the same ids name other spools there.
void tareEntryForgetAsked();
