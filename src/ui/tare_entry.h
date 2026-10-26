#pragma once

// The empty spool weight typed in (issue #40), from the weight popup's
// Options. The value goes on to the question where it is stored - spool,
// filament or brand - the same one a weighed value goes to.
void showTareEntryPad();

// The keypad, for the navigation. Closed, not hidden.
void closeTareEntry();

// The keypad is up. Counts as a modal like the weight popup:
// no automatic weighing and no question on removal while it stands.
bool isTareEntryOpen();

// Every loop pass. Closes the entry without saving once the spool it was
// opened for is lifted off the pad or another spool is loaded.
void tareEntryTick();
