#pragma once

#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>

#include "services/sort_entry_rules.h"
#include "services/sort_entry_source.h"
#include "ui/colour_search.h"

// ============================================================
//  A NEW FILAMENT SORT ENTERED AT THE SCALE: WHAT THE STEPS SHARE
//
//  The entry (sort_entry_screen.h) is one screen whose list changes from
//  step to step. sort_entry_screen.cpp holds the frame, the ways in and the
//  maker step; the keyboard lives in sort_entry_keys.cpp, the material and
//  the line step in sort_entry_steps.cpp, colour and weight in
//  sort_entry_pick.cpp. What they share is here: the state of the entry,
//  the parked navigation and the building blocks of the frame, a copy of
//  those of "New from database" (db_pick_screen.cpp), so the two look alike
//  without one file reaching into the other.
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

// SS_RASTER: the colour grid, the hue of a colour typed new.
enum SortStep : uint8_t { SS_CLOSED = 0, SS_MAKER, SS_MATERIAL, SS_LINE, SS_COLOUR, SS_RASTER, SS_WEIGHT };

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
  SN_LAST,      // the maker chosen last
  SN_MATERIAL,  // arg: an entry of sortData()->materials
  SN_LINE,      // arg: an entry of sortData()->lines
  SN_NO_LINE,   // "no line" on the line step
  SN_COLOUR,    // arg: an entry of sortData()->colours
  SN_SWATCH,    // arg: a field of the colour grid (SORT_COMMON)
  SN_HEX,       // "Enter hex" under the colour grid: the keyboard for a hue
  SN_DIAMETER,  // arg 0: 1.75 mm, 1: 2.85 mm
  SN_WEIGHT,    // arg: an entry of sortData()->weights
  SN_GRAMS      // "Type in" on the weight step: the keyboard for the grams
};

// The entry's state. entry fills step by step and starts empty at every
// open; the last choices outlive the screen, in RAM until a restart (what
// was created is in the inventory by then, plan Festlegung 8). exists is
// the name a typed one matched in its list, said once on top of the next
// step (sortExistsCaption()), empty for none. waiting: the database loads of
// the chosen material are waited for (sortMaterialLoads()), behind the
// waiting card once the first pass found them running. find: the search in
// the colour list (V40, ui/colour_search.h); its word outlives the colour
// step, so the way back from the weight step or the colour grid finds the
// list cut as it was left.
struct SortUi {
  SortEntry    entry;
  SortStep     step;
  char         last_vendor[32];
  char         last_material[17];
  char         exists[SORT_NAME_MAX];
  bool         waiting;
  ColourSearch find;
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
// The title of the colour step, with the colour search beside it
// (colourSearchTitle()); redraw builds the step again with the word.
void      sortSearchTitle(const char* text, void (*redraw)());
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

// The keyboard (sort_entry_keys.cpp): a full screen over the step with a
// title (a string id), one line of at most max_len bytes (cut to 47) and a
// line for a word under it. SK_GRAMS has the number keys and takes digits
// only, SK_HEX takes '#' and hex digits. OK parks the text without the
// blanks around it; sortKeysTick() hands it to done on the loop, where done
// may ask the inventory and build the next step. An empty text, and one of
// blanks and hyphens only (no name to sortEntrySame()), is answered with
// "please type something" and done is not called. After done the keys
// close, unless done said something under the field (sortKeysSay(): the
// text is refused, the keys stay open) or opened the keys anew. Cancel
// closes the keys and leaves the step under them as it was.
enum SortKeyKind : uint8_t { SK_TEXT = 0, SK_HEX, SK_GRAMS };
void sortKeysShow(uint8_t kind, int title_id, size_t max_len, void (*done)(const char* text));
// The same over another screen than the entry's: the keys go when under()
// is hidden or gone. The colour search of "New from database" (V40).
void sortKeysShowOver(lv_obj_t* (*under)(), uint8_t kind, int title_id, size_t max_len,
                      void (*done)(const char* text));
void sortKeysSay(const char* text);
void sortKeysClose();
// From sortEntryTick(), every pass.
void sortKeysTick();

// The keyboard's answer on the maker step: a name the inventory or the
// index has (sortEntrySame) is that maker, said on top of the material step
// (plan Festlegung 5), anything else a new one (Festlegung 7); either way on
// to sortChooseMaker(). The same on the material step against the
// materials and on the line step against the lines (sort_entry_steps.cpp).
// On the colour step (sort_entry_pick.cpp) a colour of the list is that
// entry, with its hue (and an inventory filament's own line and number,
// plan Festlegung 9); anything else is a new colour, and the colour grid
// asks for its hue.
void sortMakerTyped(const char* text);
void sortMaterialTyped(const char* text);
void sortLineTyped(const char* text);
void sortColourTyped(const char* text);
// The steps after the maker, each built from its list in sortData()
// (sort_entry_steps.cpp: material and line, sort_entry_pick.cpp: colour and
// weight).
void sortShowMaterials();
void sortShowLines();
void sortShowColours();
void sortShowWeights();
// A tap of the material or the line step (SN_MATERIAL, SN_LINE,
// SN_NO_LINE), carried out from sortEntryTick(). False for a nav it does not
// know.
bool sortStepsNav(uint8_t nav, int arg);
// The same for the colour step, the colour grid and the weight step
// (SN_COLOUR, SN_SWATCH, SN_HEX, SN_DIAMETER, SN_WEIGHT, SN_GRAMS). A weight
// chosen or typed hands the entry on: the spool type (BBS), the empty spool
// and the card, as a database entry of the picker; the entry stays under
// them at the weight step.
bool sortPickNav(uint8_t nav, int arg);
// The database loads of the chosen material, with what sortSourceTick()
// answered this pass: the waiting card while they run, then the lines and
// the line step. Cancel on the card leaves the material step on screen.
void sortMaterialLoads(SortLoad load);
// "Already there: <name>" across the top of a new step, when a typed name
// matched its list on the step before; said once.
void sortExistsCaption(lv_obj_t* body);
