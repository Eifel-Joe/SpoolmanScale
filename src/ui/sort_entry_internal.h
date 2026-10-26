#pragma once

#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>

#include "services/sort_entry_rules.h"

// ============================================================
//  A NEW FILAMENT SORT ENTERED AT THE SCALE: WHAT THE STEPS SHARE
//
//  The entry (sort_entry_screen.h) is one screen whose list changes from
//  step to step. sort_entry_screen.cpp holds the frame, the ways in and the
//  maker step; the keyboard and the steps after the maker live in
//  sort_entry_keys.cpp and sort_entry_pick.cpp. What they share is here: the
//  state of the entry, the parked navigation and the building blocks of the
//  frame, a copy of those of "New from database" (db_pick_screen.cpp), so
//  the two look alike without one file reaching into the other.
//
//  A tile's handler only parks (sortPark()); sortEntryTick() carries it out
//  on the next loop pass, where a list may be rebuilt and the inventory asked.
// ============================================================

// The frame: the header of the link lists, the list below it.
#define SORT_HEAD_H   52
#define SORT_BODY_Y   (SORT_HEAD_H + 4)
#define SORT_BODY_W   460
#define SORT_BODY_H   (320 - SORT_BODY_Y)
#define SORT_GAP      4
// Makers, materials and lines: three tiles a row. Colours: a row each.
#define SORT_TILE_W   148
#define SORT_TILE_H   44
#define SORT_ROW_W    452
#define SORT_ROW_H    44

enum SortStep : uint8_t { SS_CLOSED = 0, SS_MAKER, SS_MATERIAL, SS_LINE, SS_COLOUR, SS_WEIGHT };

// What a tap parks; arg says which entry. The steps after the maker add
// theirs.
enum SortNav : uint8_t {
  SN_NONE = 0,
  SN_OPEN,      // a way in; arg 1: at the material step of the maker the picker showed
  SN_BACK,      // one step back; from the first, to the copy popup
  SN_CLOSE,
  SN_NEW,       // "Not listed - add new" at the end of a list: the keyboard
  SN_LETTER,    // arg: 0 for a digit or a sign, 1-26 for A-Z
  SN_MAKER,     // arg: a maker of the database index (fdbMaker())
  SN_BRAND,     // arg: a brand of the inventory (sortData()->brands)
  SN_LAST       // the maker chosen last
};

// The entry's state. entry fills step by step and starts empty at every
// open; the last choices outlive the screen, in RAM until a restart (what
// was created is in the inventory by then, plan Festlegung 8).
struct SortUi {
  SortEntry entry;
  SortStep  step;
  char      last_vendor[32];
  char      last_material[17];
};
SortUi& sortUi();

// ---- navigation ---------------------------------------------------
void  sortPark(uint8_t nav, int arg);
// A tile's user data for sortOnNav(): the nav and the arg in one pointer.
void* sortNavData(uint8_t nav, int arg);
void  sortOnNav(lv_event_t* e);

// ---- the frame ----------------------------------------------------
// A scrolling list under the header in place of what the screen showed, the
// letters of the maker step included. Tiles flow in rows.
lv_obj_t* sortNewBody();
void      sortTitle(const char* text);
// A caption across the list, between groups of tiles.
void      sortCaption(lv_obj_t* parent, const char* text);
// A tile of SORT_TILE_W x SORT_TILE_H that is one label; a long text ends in dots.
lv_obj_t* sortTile(lv_obj_t* parent, const char* text, uint8_t nav, int arg);
// A row of SORT_ROW_W x SORT_ROW_H, empty, for a swatch and a name.
lv_obj_t* sortRow(lv_obj_t* parent, uint8_t nav, int arg);
// "Not listed - add new" across the list, the last tile of every list: SN_NEW.
void      sortNewTile(lv_obj_t* parent);
// A message where the list would be.
void      sortMessage(const char* text);
// The waiting card, drawn before the loop blocks on the inventory or waits
// for a load. linkWaitCardHide() takes it down.
void      sortWaitShow();

// ---- the steps ----------------------------------------------------
// A maker is chosen, or typed and found, or typed new: kept in the
// inventory's spelling when the inventory has it (sortEntrySame, plan
// Festlegung 8), the brand's filaments of the inventory read behind the
// waiting card, then the material step. The rest of the entry starts over.
void sortChooseMaker(const char* name);

// The keyboard (sort_entry_keys.cpp, Task 6): a title, one line of at most
// max_len bytes; OK hands the trimmed text to done on a later pass, Cancel
// closes the keys and leaves the step under them.
enum SortKeyKind : uint8_t { SK_TEXT = 0, SK_HEX, SK_GRAMS };
void sortKeysShow(uint8_t kind, int title_id, size_t max_len, void (*done)(const char* text));
// The keyboard's answer on the maker step (Task 6): a name the index or the
// inventory has (sortEntrySame) is that maker, anything else a new one;
// either way on to sortChooseMaker().
void sortMakerTyped(const char* text);
// The steps after the maker, each built from its list in sortData() (Task 6:
// material and line, Task 7: colour and weight).
void sortShowMaterials();
void sortShowLines();
void sortShowColours();
void sortShowWeights();
