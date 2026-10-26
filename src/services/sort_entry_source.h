#pragma once

#include <stdint.h>

#include "services/sort_entry_rules.h"

// ============================================================
//  A NEW FILAMENT SORT ENTERED AT THE SCALE: WHERE THE LISTS COME FROM
//
//  The steps of the entry (ui/sort_entry_*.cpp) offer lists, and this unit
//  fills them from two sources: the inventory of one brand, read straight
//  from the server (Spoolman, the Spoolman behind a BamBuddy, or BamBuddy's
//  own spools), and the filament database, loaded through the jobs of
//  filament_db.h. What the inventory has goes in first, in its spelling and
//  with its hue, then the database, then the usual values of
//  sort_entry_rules.h (plan, Festlegung 8); sortChoicesAdd() keeps the first
//  spelling, so a database entry the inventory has already drops out.
//
//  Everything lives in one SortData in PSRAM for as long as the entry is
//  open. The inventory is read blocking on the loop task, each request at
//  most 8 s, behind the waiting card the screen shows first; never from an
//  LVGL callback. The database loads run on filament_db's task, started and
//  collected by sortSourceTick(). Neither source is required: a server that
//  does not answer is logged with its HTTP code and leaves the lists to the
//  other source and the usual values.
// ============================================================

// One filament of a source: a filament of the inventory (Spoolman: one row
// per filament, BamBuddy: one per sort of its spools) or an entry of the
// database. name is the colour. line is the product line where the source
// keeps it apart (line_known; empty for none); without, the line is the
// front of name, the way SpoolmanDB writes it ("Matte Black"). hex is
// "RRGGBB", empty for a clear one. from is a SortFrom. filament_id is
// Spoolman's filament (Spoolman and BBS), 0 for none. name_whole: the
// filament's name gave no colour and no spool named one, so name is the
// whole filament name ("ABS Basic"): a colour of its own material only.
struct SortRow { char name[SORT_NAME_MAX]; char line[SORT_LINE_MAX]; char material[17];
                 char hex[7]; uint16_t grams; uint8_t from; bool line_known; bool name_whole;
                 int filament_id; };
#define SORT_ROWS_MAX 240
struct SortData {
  SortChoices brands;      // every inventory brand, in its own spelling
  SortChoices materials, lines, colours, weights;
  SortRow     rows[SORT_ROWS_MAX]; int row_n;
};

// Makes the SortData, empty, in PSRAM (about 56 kB). False when there is
// none to be had; the entry cannot open then. A second call starts afresh.
bool      sortSourceBegin();
// Frees it and gives the database entries back (fdbReleaseEntries()). A
// database load still running is collected and dropped by the picker's tick.
void      sortSourceEnd();
// The lists, nullptr while the entry is closed.
SortData* sortData();

// Loop task, blocking: every brand of the inventory into brands (Spoolman:
// its vendors; BamBuddy: the brands of its spools). Returns the HTTP code;
// a failure leaves brands as it was.
int       sortSourceBrands();
// Loop task, blocking: the brand's filaments of the inventory become the
// rows (those of the brand before are dropped, the database's too), then
// the materials list is built: the inventory's materials, the maker's
// materials in the database by count, every other material of the
// database, or the usual ones without a database. Returns the HTTP code of
// the inventory; the materials list is built either way.
int       sortSourceBrand(const char* brand);
// Queues the database loads for the chosen material: the maker's entries of
// it when the brand is a maker of the index and has that material, then
// those of his three most frequent other materials (plan, Festlegung 3).
// The database rows of a material chosen before are dropped. No HTTP here.
void      sortSourceMaterial(const char* brand, const char* material);
enum SortLoad : uint8_t { SL_IDLE = 0, SL_RUNNING, SL_DONE };
// Every pass while the entry is open, before dbPickTick(): starts the next
// queued load when filament_db is free, collects a finished one into the
// rows. SL_IDLE before sortSourceMaterial(), SL_RUNNING while a load is
// queued or out, SL_DONE once every one is in or given up (a failed load is
// logged and skipped).
SortLoad  sortSourceTick();
// The lists of the steps after the material, built from the rows; each
// replaces what the list held. Lines: the inventory's of this material,
// the database's, the usual ones. Colours: every filament of the inventory
// of this material whatever its line (each entry carries its row, plan
// Festlegung 9), then the database's of this material, the database's of
// other materials and the inventory's of other materials, each without a
// line or with the chosen one (an entry of another line stays out), the
// usual colours. Weights: the sizes the database knows for the brand (this
// material first, then the other materials loaded), the inventory's (this
// material first), the usual sizes.
void      sortSourceLines(const char* material);
void      sortSourceColours(const char* material, const char* line);
void      sortSourceWeights(const char* material);
