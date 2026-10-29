#pragma once

#include <stddef.h>
#include <stdint.h>

#include "services/tag_create.h"

// ============================================================
//  A NEW FILAMENT SORT ENTERED AT THE SCALE: THE RULES
//
//  A spool the scale has no tag for and the database has no entry for is
//  entered by hand: maker, material, product line, colour, the weight of a
//  full spool. Nothing is typed that can be chosen. Every step offers a list
//  made of what the filament database knows, what the inventory already
//  holds and a few usual values built into the firmware, and the keyboard is
//  the last tile of each list.
//
//  What was chosen goes on in exactly the spelling of the list it came from,
//  and what was typed is held against the list with sortEntrySame(): "light
//  green", "Light-Green" and "LightGreen" are one name, so the same colour is
//  not entered twice under three spellings and the inventory does not grow a
//  second "fiberlogy" next to "Fiberlogy". The first spelling that was added
//  to a list stays, and the sources add in order of rank: the inventory first,
//  then the database, then the usual values (plan, Festlegung 8). What the
//  inventory has already stays in its spelling and with its hue, the
//  database's spelling is only for what is new.
//
//  What the steps collect ends up in a SortEntry, and sortEntryToInput()
//  turns that into the TagCreateInput the card for a new spool reads, with
//  typed set: the spool's name must then match a filament that is found
//  (design note W, docs/plans/2026-10-26-w-sorte-eingeben.md).
//
//  The rules live here without Arduino, LVGL or a backend: a host test holds
//  them to the values in test_sort_entry_rules.py, and the screens and the
//  sources of the lists ask this unit instead of keeping rules of their own.
//  The comparison reads a character at a time through the fold of
//  services/text_fold.h (V43), the one the search in a colour list uses too:
//  case is no difference, the capital umlauts and accents of Latin-1 included,
//  and a letter with its accent is still not the letter without it. No locale
//  and no half-folded UTF-8 sequence can sneak in. Whatever is cut to a field
//  is cut at a character boundary (utf8Cut), never through a sequence.
// ============================================================

#define SORT_NAME_MAX        48       // a name in a list, as TagCreateInput::color_name
#define SORT_LINE_MAX        32       // a product line, as TagCreateInput::subtype
#define SORT_CHOICES_MAX     96       // the most entries one list holds
#define SORT_GRAMS_MIN       50       // the lightest full spool that can be entered
#define SORT_GRAMS_MAX    10000       // the heaviest
#define SORT_COMMON_COUNT    28       // the usual colours
#define SORT_LINES_COUNT     18       // the usual product lines
#define SORT_SIZES_COUNT      5       // the usual spool sizes in grams
#define SORT_MATERIALS_COUNT 12       // the usual materials

// Where an entry of a list comes from; the screens group and rank by it.
enum SortFrom : uint8_t {
  SF_LAST = 0,   // chosen last, which is offered first
  SF_DB,         // the filament database, for this maker and material
  SF_DB_OTHER,   // the filament database, for other materials of this maker
  SF_OWNED,      // the inventory
  SF_USUAL       // the usual values built into the firmware
};

// One entry of a list. A name list leaves hex empty and grams 0, a colour
// list gives the hue as "RRGGBB" (an empty hex there is the clear one, no
// hue), a weight list gives grams and names the entry "850 g". row is the
// row of the source that the entry stands for (a filament of the inventory
// that was read), -1 for none: sortChoicesAdd() and sortChoicesAddGrams()
// leave it at -1, and the source sets it where it has a row.
struct SortChoice  { char name[SORT_NAME_MAX]; char hex[7]; uint16_t grams; uint8_t from; int16_t row; };
// A list, filled by the sources and by the usual values. n is the count in use.
struct SortChoices { SortChoice item[SORT_CHOICES_MAX]; int n; };
// A usual colour: its name in the spelling of the filament database, its hue
// as "RRGGBB"; an empty hex is the clear one.
struct SortColour  { const char* name; const char* hex; };

// The usual values. Colours are the 28 of the colour grid, lines are what a
// maker's name for a filament starts with when the database has no field for
// it, sizes are in grams (1000 g first), materials are what to offer when no
// database says which ones the maker sells.
extern const SortColour      SORT_COMMON[SORT_COMMON_COUNT];
extern const char* const     SORT_LINES[SORT_LINES_COUNT];
extern const uint16_t        SORT_SIZES[SORT_SIZES_COUNT];
extern const char* const     SORT_MATERIALS[SORT_MATERIALS_COUNT];

// What the steps have collected. The fields are as long as the ones of
// TagCreateInput that they end up in, and they always arrive terminated (the
// screens fill them with utf8Cut). A line is empty when the sort has none; a
// clear filament has no hex (clear). The diameter is 1.75 or 2.85 mm.
// filament_id is the id of a filament of the inventory that was picked
// instead of being typed or composed, 0 for none.
struct SortEntry { char vendor[32]; char material[17]; char line[SORT_LINE_MAX]; char colour[SORT_NAME_MAX];
                   char hex[7]; bool clear; int grams; float diameter_mm; int filament_id; };

// True when both names are equal once case, blanks and '-' are left out:
// "Light Green", "light-green" and "LightGreen" are one name, " ABS " and
// "abs" too. Case is the fold of textFoldNext(): the capital umlauts and
// accents of Latin-1 are their small letters, so a name typed with a small u
// umlaut is the one stored with the capital. A letter with its accent is not
// the letter without it (an e acute is not an e, a u umlaut is not "ue"). A
// name with nothing left is never equal to anything, not even to another one
// with nothing left, and neither is a null name.
// Wurzel: the keyboard types umlauts and accents now (QWERTZ, AZERTY); the
//   fold of A-Z alone kept the small and the capital umlaut apart, so a name
//   typed with the small one entered a second filament next to the stored
//   one with the capital.
// Fix: both names are read a character at a time through textFoldNext().
// NOT-TO-DO: a fold of bytes (c | 0x20) takes one UTF-8 sequence for another
//   (0x9C for 0xBC); tolower() follows the locale.
// See test_sort_entry_rules.py.
bool sortEntrySame(const char* a, const char* b);

// The index of the entry of the list that is the same name (sortEntrySame),
// or -1 when there is none, for an empty name and for a null name.
int sortChoicesFind(const SortChoices& list, const char* name);

// Adds an entry unless the list has the same name already, and returns the
// index of the new one or of the one that was there: the first spelling, hex
// and origin stay, whatever is added later under another spelling is dropped.
// That holds when the list is full, too. A name longer than a field is cut at
// a character boundary (and the cut name is what meets the list). The new
// entry has no source row (row is -1). hex may be null (taken as empty); a
// colour is kept as six capitals without '#', anything else is kept as it is
// (cut to six bytes), so that sortEntryToInput() refuses it later and it is
// not taken for the empty hex, which in a colour list is the clear one.
// Returns -1 for a null name, for one with nothing left (empty, or only blanks
// and hyphens) and for a full list (SORT_CHOICES_MAX) that does not have it.
int sortChoicesAdd(SortChoices* list, const char* name, const char* hex, uint8_t from);

// The same for the weight of a full spool: the entry is named "850 g" and
// carries the grams, and one weight is on the list once. Returns -1 for a
// weight outside SORT_GRAMS_MIN to SORT_GRAMS_MAX (both ends in) and for a
// full list that does not have it.
int sortChoicesAddGrams(SortChoices* list, int grams, uint8_t from);

// Reads a colour typed as hex: six hexadecimal digits, with or without a
// leading '#', in any case. out becomes the digits in capitals, "90EE90"
// (text and out may be the same array). False for anything else: too short
// or too long, a letter beyond F, empty, null; out stays as it was.
bool sortEntryHex(const char* text, char out[7]);

// The usual line (SORT_LINES) that the name starts with as a whole word, in
// any case, the two-word "High Speed" too: "Matte Black" starts with "Matte",
// "Silky Gold" does not start with "Silk". A word ends at the end of the name,
// at a blank or at a dash (the way tagCreateStartsWithWord() reads a name).
// Returns the line as spelled in SORT_LINES, nullptr for none and for a null
// name. SpoolmanDB has no field for a line; it stands in front of the colour.
const char* sortEntryLineOf(const char* name);

// Whether the filament of a list entry belongs to the line that was chosen,
// and the colour it carries for it. With own_line not null, the entry has its
// line apart ("" is none) and name is the colour already; otherwise the line
// is read from the name (sortEntryLineOf) and cut off the colour, and a name
// that starts with no usual line has none. True when the line of the entry is
// chosen_line (sortEntrySame, and an entry without a line belongs to a
// chosen line that is empty: both empty count as the same). Then out holds
// the colour; it holds nothing for False.
bool sortEntryColourOf(const char* name, const char* own_line, const char* chosen_line, char* out, size_t out_size);

// Reads the name of a filament of the inventory (Spoolman style: the material
// may be in front, then a usual line, the rest is the colour) into its line
// and its colour: "PETG Basic" gives "Basic" and no colour, "Matte Light
// Green" gives "Matte" and "Light Green", "ASA" of the material ASA gives
// neither, and "PETG Rapid-Eco Dark Green" gives no line (Rapid-Eco is none
// of the usual ones) and "Rapid-Eco Dark Green" as the colour. line and
// colour are empty when the name says nothing of them.
void sortEntrySplitName(const char* name, const char* material, char line[SORT_LINE_MAX], char colour[SORT_NAME_MAX]);

// Reads the value of a Spoolman extra field, which is kept as JSON text, as
// the text it holds: the string "\"Light Green\"" (quotes included) gives
// Light Green. The escapes of a quote, a backslash and a slash inside it are
// resolved, and so is the u escape (a backslash, u and four hex digits, the
// way Python's json.dumps writes a non-ASCII character): it gives the UTF-8 of
// that character (the escape of the small u umlaut, 00FC, gives the two bytes
// C3 BC). A surrogate half (D800 to DFFF) and the escape of 0 give '?'.
// Any other escape stays as it was written. The text is cut to out_size - 1
// bytes at a character boundary, and to 2 * SORT_NAME_MAX - 2 bytes at the
// most. False, with out empty, for a null or empty value, for the empty
// string ("\"\""), for anything that is not a JSON string (a number, null)
// or has lost its closing quote, for a u escape with fewer than four hex
// digits, and for an unusable out.
bool sortEntryExtraText(const char* raw, char* out, size_t out_size);

// Turns what was collected into the input of the card for a new spool, from
// scratch: nothing of the old content stays. The database fields are empty
// (db_id, db_name), the article is unknown, names_known and typed are set,
// the product is the material and, with a line, the line after it ("ABS
// Matte"; cut to a character boundary within its field), the line is the
// subtype, color_name and color_name_en are the colour, a hue gives one
// colour and rgba with full alpha ("90EE90FF"), a clear filament none and
// "00000000" (the hex of a clear entry is not looked at). A line of blanks
// is no line. The diameter is 1.75 mm unless the entry says 2.85. No empty
// spool weight is set (spool_weight_g, spool_type_g stay 0). known_filament_id
// is the entry's filament_id (0 for none). Every text is cut to its field at
// a character boundary.
// False, and nothing to use, when the vendor, the material or the colour is
// empty, when the hex is no colour and the filament not clear, and when the
// weight lies outside SORT_GRAMS_MIN to SORT_GRAMS_MAX.
bool sortEntryToInput(const SortEntry& e, TagCreateInput* in);
