#pragma once

// ============================================================
//  WHICH TAG A CREATE CARD BELONGS TO: THE RULES
//
//  The card that creates a spool (from a Bambu or Creality tag, from "new
//  from database", from "enter a sort") is opened for the spool on the pad
//  and links its tag at the end. Another spool laid on while the card or the
//  spool type list in front of it stands would have its weight taken and the
//  first spool's tag linked to it (Punkteliste n04). The card keeps the key of
//  the tag it was opened for and goes when another one is read.
//
//  The rules live here, without Arduino or the screens: a host test holds them
//  to the values in test_create_tag_rules.py.
// ============================================================

// The key a tag is told by: a Bambu tag by its tray uuid (32 characters),
// which both chips of a spool share, any other tag by the reader's UID. Never
// null: "" when neither is there.
const char* createPadTagKey(const char* tray_uuid, const char* uid_str);

// True while the card still belongs on the screen, asked with what the reader
// has now (tray_uuid and uid_str as g_tag holds them): the card holds no tag
// (a create without a tag weighs nothing), nothing is read now (the reader
// lost it, or the display was cleared), or the key read is the one it holds.
// One more case for a card that holds a Bambu spool: a 4-byte UID without its
// tray uuid is a Bambu read that has not settled - the scan takes a first,
// partial attempt at once when the UID changes, which is what the other chip
// of the same spool, or the same chip read again, looks like until a retry
// brings block 9. Another key is another spool on the pad.
bool createCardTagFits(const char* held, const char* tray_uuid, const char* uid_str);
