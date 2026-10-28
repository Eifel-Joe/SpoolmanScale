#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "services/colour_search_rules.h"

struct FdbEntry;

// ============================================================
//  THE SEARCH IN A COLOUR LIST, ON THE PANEL (V40)
//
//  "Enter filament" (sort_entry_pick.cpp, the colour step) and "New from
//  database" (db_pick_screen.cpp, the colour families and the entries) share
//  this: a magnifier in the header, left of the close button. Its tap parks;
//  the screen's tick (colourSearchTick()) opens the entry's keyboard over that
//  screen, full screen. OK hands the word back, and the list is built once
//  with the names that hold it (colourSearchMatch()); the header then shows
//  the word with an x in place of the magnifier, and a tap on it ends the
//  search: the whole list again. Cancel on the keyboard leaves the list as it
//  was. Another word is typed after the x.
//  Wurzel: the WT32 has room for the keyboard or the list, not both, and a
//    list built again at every key would churn the LVGL pool (spec 5.3 X).
//  Fix: keyboard and list one after the other; the list is built once per word.
//  NOT-TO-DO: a field over the list that filters while typing: the keyboard
//    takes half the screen and the list is rebuilt at every key.
//
//  Each screen keeps one ColourSearch, zero to begin with. The word belongs to
//  the list it was typed for: back on that step ends the search first and the
//  step stays (colourSearchBack()), a close forgets it (colourSearchClose()).
//  The steps after it leave it alone, so the way back to the list finds it cut
//  as it was left. See test_colour_search.py.
//
//  "New from database": a search on the family step shows entries and stays
//  the family step (db_pick_screen.cpp, showEntries() and showFamilyStep()).
//  Wurzel: the families cut a long list by colour, and "bone" must be found
//    whichever family it is in: from the family step the search lists the
//    entries of every family, which only showEntries() can draw, and
//    showEntries() made the step the entries step.
//  Fix: showEntries() sets DBP_ENTRIES only when it is not called on the
//    family step with a word, showFamilyStep() hands a word to showEntries(),
//    and the redraw is showFamilyStep() while the step is the family step:
//    the x and back show the families again, the next back goes to the lines
//    or the materials as before.
//  NOT-TO-DO: switching to the entries step with every family (s_family -1):
//    its x and back would then show all entries of the line, not the
//    families the search started from. A step of its own for the search
//    would reach into goBack() and every caller of the steps, in a file at
//    its 1000 lines.
// ============================================================

struct ColourSearch {
  char      word[COLOUR_SEARCH_MAX];   // "" for the whole list
  lv_obj_t* head;                      // the magnifier or the word, nullptr while none is shown
  void    (*redraw)();                 // builds the step on screen again; nullptr on a step without a search
  bool      tapped;                    // parked by the head
  bool      typed;                     // a word came from the keyboard
};

// The header's title, and the search beside it when redraw is not null: the
// magnifier, or the word with its x, left of the close button, the title
// narrowed to end before it. With redraw null the head goes and the title has
// the width of the frame again (DBP_HEAD_*, SORT_HEAD_*). Every step of the
// two screens calls this in place of setting its title; redraw is what builds
// that step again.
void colourSearchTitle(ColourSearch* s, lv_obj_t* scr, lv_obj_t* title, const char* text, void (*redraw)());

// From the screen's tick, every pass. A tap on the magnifier opens the
// keyboard over under(); a word that came back, or a tap on the word (its x),
// builds the step again through redraw. Dropped when under() is gone or
// hidden, or the step on screen has no search.
void colourSearchTick(ColourSearch* s, lv_obj_t* (*under)());

// Back on a step with a word: the word goes and the step is built again
// (true). Without a word false, and back does what it did before.
bool colourSearchBack(ColourSearch* s);

// The screen closes: the word goes, and the head with the screen it is on.
void colourSearchClose(ColourSearch* s);

// Whether a name of the list is shown: always without a word.
bool colourSearchShows(const ColourSearch* s, const char* name);
// The same for an entry of the database, by the name its row shows
// (fdbEntryDisplayName()), without the maker and the material.
bool colourSearchFdb(const ColourSearch* s, const FdbEntry& e, const char* maker, const char* material);

// "No colour with "bone"" across the list, for a search that found nothing:
// under a word only, and where the LVGL pool has room for a row.
void colourSearchNone(const ColourSearch* s, lv_obj_t* body, int width);
