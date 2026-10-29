#pragma once

// ============================================================
//  CREATE SPOOL WITHOUT A TAG: THE FLAG
//
//  X (V35): "Create spool" on the empty main screen opens the "New / Copy"
//  choice with no tag on the pad. The rules of what changes while it lasts
//  are in services/no_tag_create_rules.h; this is the one place that holds
//  the answer to "are we in it", for the choice, the card and the copy
//  confirmation alike.
//
//  Wurzel: the answer cannot be read off the pad each time it is asked. A tag
//  that is laid on half way through (the roll is stuck with its label while
//  the card is up) would flip the mode in the middle of a create, and the
//  reading of the roll lying on the dish would come back in. So the choice
//  decides once, when it opens, and the flag keeps that decision until
//  another way into creating clears it.
//  NOT-TO-DO: deriving it from link_tag_uid on every call; that is the live
//  reading this flag exists to freeze.
//  A cancel does not clear it (the database and the sort entry close the
//  choice and still need it); the rule for the tiles (noTagSourceOffered)
//  makes a flag left on harmless for a Bambu tag's card.
//  See test_no_tag_entry.py.
// ============================================================

// Sets the flag. Writes "NoTag: mode on|off" to the log when the value
// changes, nothing when it is set to what it is. Every call, a change or not,
// puts the count of several spools back to one (batch_create.h).
void noTagModeSet(bool on);

// Whether a create without a tag is under way.
bool noTagMode();

// The tap on the main screen's copy button: opens the "New / Copy" choice. With
// nothing on the pad and nothing found (the button reads "Create spool") it
// first drops what a lifted tag still has up, see the .cpp.
void noTagEntryTap();

// The title of the "New / Copy" choice: named like the button that opens it,
// so "create spool" in the flag, "New / Copy" otherwise.
const char* noTagEntryTitle();

// The end of a create without a tag, from the loop: closes the picker's
// screens and the choice, shows the main screen with the new spool on it by
// its number (as after "spool by ID", nothing is linked), says so in green
// (the Bambu Lab note when the vendor is Bambu's), and clears the flag.
// vendor is the maker of the new spool, "" when not known. Only when the new
// spool is the one on the screen after all (a tag laid on meanwhile takes the
// screen), it also arms the location picker (V39), which noTagLabelAskTick()
// opens; with a label printer ready the question for a label follows once
// the picker has ended.
void noTagCreateFinish(int new_spool_id, const char* vendor);

// The end of a run of several spools (batch_create.h), with at least one made:
// ids holds the `made` numbers in the order made, `wanted` is the count typed
// in. The first spool is shown and asked about exactly as noTagCreateFinish()
// does it; with several, the green line names how many (the Bambu Lab note
// when the vendor is Bambu's). reason says why the run stopped ("Spool 4:
// ..."; after a lost answer it sends the user to the inventory), "" or nullptr
// when every spool was made. ids and reason live only for the call: anything
// kept beyond it must be copied. Does nothing when ids is null or made below 1.
void noTagBatchFinish(const int* ids, int made, int wanted, const char* vendor, const char* reason);

// After noTagCreateFinish(): whether a yellow line (the parts a copy left
// behind, a price the server did not keep) may be written to the status line
// now. It may not while the Bambu Lab chip hint stands there: the hint is the
// last word, and what was lost goes to the log with the spool's number
// instead. lost names it for the log; parts is the bits of what a copy left
// behind (CM_*), 0 where there are none.
bool noTagYellowLineAllowed(int new_spool_id, const char* lost, unsigned parts);

// The loop's part of what follows the ending: a pass after it, the location
// picker for the new spool (the list of "More info" -> "Location", whatever
// "location on removal" says); once the picker has ended, the card for a
// label a pass later. It carries out the answer, and closes the card without
// printing when the new spool is no longer the one on the screen.
void noTagLabelAskTick();

// Whether the card is up, so that the loop holds off its own questions.
bool noTagLabelAskOpen();

// Whether the create's own questions are still to come or open: from the
// ending until the last of them is answered (the location picker armed, asked
// for or up; then the label question armed or its card up). The questions
// after a lift and the lift clock of the spool wait for this.
bool noTagAfterCreateBusy();

// Takes the card down and forgets a question, or a picker, that was armed and
// not yet asked, without printing: any other screen is shown
// (hideAllOverlays()). A picker already up is the picker's own: a navigation
// takes it down without a question for a label after it.
void closeNoTagLabelAsk();
