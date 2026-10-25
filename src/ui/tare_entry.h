#pragma once

// The empty spool weight, weighed or typed in (issue #40). Opened from the
// weight popup's empty spool button. Either way the value goes on to the
// popup that asks where it is stored - spool, filament or brand.
void showTareChoice();

// Both dialogs of this file, for the navigation, and with them the empty
// spool screen of the settings and a name entry for a new spool type
// (empty_spool_type.h, spool_type_name.h). Closed, not hidden.
void closeTareEntry();

// The choice, the keypad or the empty spool screen is up. Counts as a modal
// like the weight popup: no automatic weighing and no question on removal
// while it stands.
bool isTareEntryOpen();

// Every loop pass. Runs the ticks of the name entry and the empty spool
// screen, then closes the choice or the keypad without saving once the spool
// they were opened for is lifted off the pad or another spool is loaded. The
// empty spool screen belongs to no spool and stays.
void tareEntryTick();
