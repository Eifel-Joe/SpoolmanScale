#pragma once

#include <stdint.h>

#include "app_config.h"   // NEW_SPOOL_TARE_MIN_G, BAMBU_VENDOR_NAME, defines only

// ============================================================
//  A SPOOL CREATED WITHOUT A TAG AND WITHOUT WEIGHING: THE RULES
//
//  X (V35, "docs/entwurf/2026-10-27-x-neue-spule-ohne-wiegen.md"): "Create
//  spool" with nothing on the screen opens the same "New / Copy" choice as
//  for an unknown tag, but there is no tag to link and nothing is weighed. A
//  roll still in its wrapping is put into the database as full and unused;
//  the tag is stuck on later and linked the way it is today.
//
//  The mode is a flag ("no tag") that lives for the length of one create. It
//  is set when the choice opens with neither a Bambu tag nor an id (a tag UID)
//  on the pad, and it is cleared at any other way into creating and at the end
//  of a create. While it is on:
//
//    sources   The database and "enter sort" are offered without an id.
//    reading   The scale counts as empty: the start rule, the guess at the
//              fill weight and the question for the spool type all see 0 g,
//              even with something lying on the dish.
//    type      The spool type is asked even without a weight.
//    fill      A fill weight that is missing (0 or less) is asked for, and
//              what is typed in is valid from NO_TAG_FILL_MIN_G to
//              NO_TAG_FILL_MAX_G, both ends in.
//    end       No linking. The new number is shown as after "spool by ID".
//
//  The rules live here, without Arduino, the backend or the screens: a host
//  test holds them to the values in test_no_tag_create.py, and the screens
//  ask this unit instead of keeping rules of their own.
// ============================================================

#define NO_TAG_FILL_MIN_G   100    // the least filament a spool is typed in with
#define NO_TAG_FILL_MAX_G  5000    // the most (a 5 kg roll)

// Whether the flag goes on when the "New / Copy" choice opens: no Bambu tag
// (g_tag.tray_uuid) and no id (link_tag_uid) on the pad.
bool noTagModeAtEntry(bool bambu_tag, bool have_uid);

// Whether the database and "enter sort" tiles are offered. In the flag always;
// otherwise as before: an id is there and it is no Bambu tag (the Bambu tag
// has its own card).
// Wurzel: a cancel leaves the flag on, and a Bambu tag laid on afterwards shows
// the "not in BamBuddy" card, which must not grow the tiles of the no-tag mode.
// Fix: a Bambu tag always wins over the flag.
// NOT-TO-DO: `no_tag || have_uid` alone looks the same on a fresh entry, but
// it turns a left-over flag into extra tiles on a Bambu tag's card.
// See test_no_tag_create.py (the line with no_tag and bambu_tag both true).
bool noTagSourceOffered(bool no_tag, bool bambu_tag, bool have_uid);

// The reading the create works with: 0 g in the flag, otherwise the bare
// reading. Wurzel: the card, the copy and the question for the type take the
// weight on the dish as the spool, and the wrapped roll that is being entered
// lies there (a roll, a box, a bag of desiccant). NOT-TO-DO: using the reading
// when "it looks like a spool" (above NEW_SPOOL_TARE_MIN_G): a roll is no
// empty spool, the fill weight and the tare would both be wrong.
float noTagGross(bool no_tag, float bare);

// Whether the spool type is asked (the list of Bambuddy's catalog of empty
// spools) when the type list is active (BamBuddy) and none is chosen yet
// (type_g 0 or less): in the flag always, since there is no weight to show for
// it; otherwise only with something on the pad, a reading above
// NEW_SPOOL_TARE_MIN_G. A reading that is not a number counts as an empty pad.
bool noTagTypeAsk(bool types_active, bool no_tag, float bare, int type_g);

// Whether the fill weight is missing and must be asked for: in the flag, a
// label weight of 0 or less. Without the flag the card guesses from the
// reading as before, so nothing is missing there.
bool noTagFillMissing(bool no_tag, int label_g);

// Whether a typed-in fill weight is valid: NO_TAG_FILL_MIN_G to
// NO_TAG_FILL_MAX_G, both ends in. Not a number is not valid.
bool noTagFillPlausible(float grams);

// What the note after the create says. NTN_BAMBU_CHIP: "put the chip on before
// the first AMS". Wurzel: Bambuddy makes a spool itself when an unknown Bambu
// roll goes into an AMS with a reader (spec 3.1), so a spool made here would be
// there twice. NTN_PLAIN: "created without tag".
enum NoTagNote : uint8_t { NTN_PLAIN = 0, NTN_BAMBU_CHIP };

// The note for a spool of this vendor: "Bambu Lab" or "Bambu", blanks around
// it and the case ignored (ASCII only), is the chip note; any other vendor, an
// empty one and a null pointer are plain.
// NOT-TO-DO: a prefix match ("Bambu Lab - ..."): the vendor is the maker only,
// and a maker named "Bambulab" or "Bambu Third" is not Bambu's.
NoTagNote noTagCreatedNote(const char* vendor);

// Whether "into AMS" is offered under "more info": the server can assign a
// slot, an AMS is there, a spool is on the screen that is not archived and
// has a number. With or without a load cell, with or without a tag.
bool amsInfoOffered(bool can_assign, bool has_ams, bool found, bool archived, int spool_id);
