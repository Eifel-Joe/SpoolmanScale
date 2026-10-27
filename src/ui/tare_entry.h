#pragma once

// The empty spool weight, weighed or typed in (issue #40). Opened from the
// weight popup's empty spool button. Either way the value goes on to the
// popup that asks where it is stored - spool, filament or brand. With
// Bambuddy in Spoolman mode two by two (design L13): weigh and type in, a
// type from the list of spool types and the full spool, weighing and the full
// spool only with the spool on the pad. A type goes to the spool as its empty
// weight; the full spool asks for its filament, offers the types near the
// reading less that, and stores the pick, then label weight and rest
// (confirmParkFullSpool()). Every other backend keeps the column of weigh and
// type in.
void showTareChoice();

// The first weighing (Bambuddy in Spoolman mode, design 3): a spool with a
// tag that lies still on the pad is asked once per session which empty spool
// it sits on, with the list of spool types (spool_type_list.h) as
// spoolTypeFirstAsk() decides. A type picked is stored as the spool's empty
// weight and the rest follows it (V19, as for the full spool, label weight
// raised first where the spool holds more); "take measured" stores the empty
// weight alone; "later" holds until the next restart. Opened from
// tareEntryTick().

// Both dialogs of this file, for the navigation, and with them the empty
// spool screen of the settings, the list of spool types and a name entry for
// a new spool type (empty_spool_type.h, spool_type_list.h, spool_type_name.h).
// Closed, not hidden, without their callbacks.
void closeTareEntry();

// The choice, the keypad, a list of spool types opened from here (the first
// weighing, the choice) or the empty spool screen is up, or a pick from the
// choice's list waits for its write on the next pass. Counts as a modal like
// the weight popup: no automatic weighing and no question on removal while it
// stands.
bool isTareEntryOpen();

// Automatic weighing waits for the first weighing: true while the spool on
// the pad may still be asked (it passes the first weighing's own conditions,
// a dialog standing or not, and was not looked at yet), and from the moment
// its question opened until its tag is gone or another spool is loaded. A type
// picked writes the rest itself and "take measured" leaves it as it was, and
// "later" leaves rest and tare untouched, so the question comes again after a
// restart.
bool tareEntryHoldsAutoWeigh();

// Every loop pass. Runs the ticks of the list of spool types, the name entry
// and the empty spool screen and opens the first weighing, then closes the
// choice, the keypad or a list opened from here without saving once the
// spool they were opened for is lifted off the pad or another spool is
// loaded. The empty spool screen belongs to no spool and stays.
void tareEntryTick();

// Forgets which spools the first weighing has looked at, for a backend switch:
// the same ids name other spools there.
void tareEntryForgetAsked();

// A spool just created at the scale whose type our list asked on the way (a
// type or "without type" answered, "new from database", the copy, or the card
// for a Bambu tag, V33): the first weighing counts it as looked at, like a
// spool it asked itself, so the same question does not come again right after
// it (Jörg, 26.10.2026); like every spool looked at, until a restart or a
// backend switch. Called with the new id at the create's success point, only
// where the create's own input says the list was answered; a spool created
// from a tag without the list (any other tag, or a Bambu tag with nothing on
// the pad) is asked as before.
void tareEntryFirstWeighDone(int spool_id);
