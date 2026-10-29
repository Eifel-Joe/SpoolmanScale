#pragma once

#include <stdint.h>

// ============================================================
//  WHICH EMPTY SPOOL: THE LIST OF SPOOL TYPES (design A1, A2)
//
//  A full screen with the types of Bambuddy's catalog of empty spools
//  (spool_types.h) as a scrolling list, one row per type with its weight,
//  and a bar of up to four buttons under it. A tap on a row picks that type.
//
//  What it shows depends on the mode, and the buttons change it:
//
//    STL_NEAR    the types within SPOOL_TYPE_NEAR_G of a computed empty
//                weight, the closest first and marked, at most
//                SPOOL_TYPE_NEAR_MAX (a full spool on the pad, design A2).
//                Buttons: take the measured weight, other type (VENDOR),
//                add a new type, skip; the first and the third with a
//                measured weight only. An empty weight no spool can have
//                (spoolTypeTarePlausible) shows a line saying so instead,
//                with other type and skip only.
//    STL_VENDOR  the types whose name begins with the spool's maker
//                (spoolTypeFilter, design note L3); none at all makes it
//                ALL. Buttons: show all, add a new type (with a measured
//                weight only), skip.
//    STL_ALL     every type. Buttons: only the maker's (where it has any);
//                where it has none (VENDOR fell to ALL), add a new type (with
//                a measured weight only); skip. Reached by "show all", it
//                has no "add new": VENDOR has it, one tap away.
//
//  In VENDOR and ALL the type nearest to preselect_g is marked, at any
//  distance; the list scrolls to the marked row.
//
//  In VENDOR and ALL the header has the magnifier of the colour lists
//  (ui/colour_search.h, V43). A word lists the types of the whole catalog
//  that hold it, the maker's or not, and says so when none does; the mark is
//  set on a row that is shown only. The word with its x in the header, back,
//  "show all" and "only the maker's" end the search; a close forgets it.
//
//  The catalog is loaded fresh when the list opens, from the loop; a load
//  that fails is no list here (the failure stands in the list, the buttons
//  stay). The buttons that change the mode build the rows again from the
//  same load. "Add new" hides the list and opens the name entry
//  (spool_type_name.h) with the maker and a blank filled in and the
//  measured weight; a type created there is answered like a picked one, a
//  cancel brings the list back with its rows built again (the name entry
//  loaded the catalog fresh).
//
//  Rows and buttons only park what was tapped; spoolTypeListTick() closes
//  the list and then calls back, from the loop. The name passed to on_pick
//  is a copy that stays good until the next pick.
//
//  Bambuddy in Spoolman mode only (spoolTypesActive()), unless the request
//  says any_inventory: then Bambuddy in either inventory mode is enough
//  (spoolTypesCatalogActive(); V33, the card for a new spool from a Bambu tag
//  alone). It belongs to no spool by itself: whoever opens it for a spool
//  decides what closes it.
// ============================================================

enum SpoolTypeListMode : uint8_t { STL_NEAR, STL_VENDOR, STL_ALL };
struct SpoolTypeListReq {
  SpoolTypeListMode mode;
  char  vendor[32];     // the spool's maker; "" shows all
  float near_g;         // STL_NEAR: the computed empty weight
  float preselect_g;    // VENDOR/ALL: the type nearest to this is marked; 0: none
  float measured_g;     // > 0: "take measured" (NEAR) and "new" with this weight
  int   skip_text;      // STR_SPT_LATER, STR_SPT_WITHOUT or STR_CANCEL
  // True: Bambuddy in either inventory mode opens the list. False (every
  // request built with = {}): Bambuddy in Spoolman mode only. Every caller
  // initialises its request with = {}, so the callers that do not know this
  // field stay on Spoolman mode.
  bool  any_inventory;
};
typedef void (*SpoolTypePicked)(float grams, const char* name);  // "" for the measured value
// Not Bambuddy in Spoolman mode (any_inventory: not Bambuddy at all): logged,
// nothing opens, and on_skip is called at once, so the caller goes on as after
// a skip. "Take measured" and "add new" need a measured weight a spool can
// have (spoolTypeTarePlausible); the name entry would refuse any other.
void showSpoolTypeList(const SpoolTypeListReq& req, SpoolTypePicked on_pick,
                       void (*on_skip)(), void (*on_back)() = nullptr);  // null: back = skip
bool isSpoolTypeListOpen();
void closeSpoolTypeList();      // no callback; closes a name entry opened from it
void spoolTypeListTick();       // loop: load, parked answer
