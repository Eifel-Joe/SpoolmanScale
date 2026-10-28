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
// changes, nothing when it is set to what it is.
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
