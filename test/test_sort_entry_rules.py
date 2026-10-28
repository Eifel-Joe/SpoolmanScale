"""Compile the production rules of a filament sort entered at the scale and check them: when two names are one, the lists with the first spelling kept, the hex a colour is typed in, the usual line in front of a name, the text of a Spoolman extra, and the input the card for a new spool gets (services/sort_entry_rules.h)."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/sort_entry_rules.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

// The limits, spelled out here on purpose: the production defines must agree
// with this text, not with themselves. A name of 47 bytes and a terminator,
// a line of 31, 96 entries to a list, a full spool from 50 g to 10 kg, and
// the sizes of the tables.
static_assert(SORT_NAME_MAX == 48, "as TagCreateInput::color_name");
static_assert(SORT_LINE_MAX == 32, "as TagCreateInput::subtype");
static_assert(SORT_CHOICES_MAX == 96, "96 entries to a list");
static_assert(SORT_GRAMS_MIN == 50, "the lightest full spool");
static_assert(SORT_GRAMS_MAX == 10000, "the heaviest full spool");
static_assert(SORT_COMMON_COUNT == 28, "28 usual colours");
static_assert(SORT_LINES_COUNT == 18, "18 usual lines");
static_assert(SORT_SIZES_COUNT == 5, "5 usual sizes");
static_assert(SORT_MATERIALS_COUNT == 12, "12 usual materials");
// What is collected fits what it is copied to, field for field.
static_assert(sizeof(SortEntry::vendor) == sizeof(TagCreateInput::vendor), "vendor");
static_assert(sizeof(SortEntry::material) == sizeof(TagCreateInput::material), "material");
static_assert(sizeof(SortEntry::line) == sizeof(TagCreateInput::subtype), "line is the subtype");
static_assert(sizeof(SortEntry::colour) == sizeof(TagCreateInput::color_name), "colour name");
static_assert(sizeof(SortEntry::hex) == sizeof(TagCreateInput::color_hex), "hex");
static_assert(sizeof(SortChoice::name) == sizeof(TagCreateInput::color_name), "a name in a list");
static_assert(sizeof(SortChoice::hex) == sizeof(TagCreateInput::color_hex), "hex");

// True when the text is well formed UTF-8: every lead byte is followed by
// its continuation bytes and by nothing else. A cut through a sequence fails.
static bool utf8Ok(const char* s) {
  const unsigned char* p = (const unsigned char*)s;
  while (*p) {
    int extra = *p < 0x80 ? 0 : (*p & 0xE0) == 0xC0 ? 1 : (*p & 0xF0) == 0xE0 ? 2 : (*p & 0xF8) == 0xF0 ? 3 : -1;
    if (extra < 0) return false;
    p++;
    for (int i = 0; i < extra; i++, p++) {
      if ((*p & 0xC0) != 0x80) return false;
    }
  }
  return true;
}

// What the entry of the test turns into the input from: Fiberlogy ABS in
// light green, 850 g, 1.75 mm.
static SortEntry fiberlogy() {
  SortEntry e = {};
  strcpy(e.vendor, "Fiberlogy");
  strcpy(e.material, "ABS");
  strcpy(e.colour, "Light Green");
  strcpy(e.hex, "90EE90");
  e.grams = 850;
  e.diameter_mm = 1.75f;
  return e;
}

// Whether the entry is accepted. The input starts as garbage so that a
// field the function does not set shows.
static bool accepted(const SortEntry& e) {
  TagCreateInput in;
  memset(&in, 0x5A, sizeof(in));
  return sortEntryToInput(e, &in);
}

int main() {
  // Festlegung 5: case, blanks and hyphens make no other name.
  assert(sortEntrySame("Fiberlogy", "fiberlogy"));
  assert(sortEntrySame("Light Green", "light-green") && sortEntrySame("Light Green", "LightGreen"));
  assert(sortEntrySame(" ABS ", "abs"));
  assert(!sortEntrySame("Light Green", "Light Greens") && !sortEntrySame("PLA", "PLA+"));
  assert(!sortEntrySame("", "") && !sortEntrySame("-", " "));
  assert(sortEntrySame("Caf\xC3\xA9", "CAF\xC3\xA9") && !sortEntrySame("Caf\xC3\xA9", "Cafe"));
  // A capital umlaut is a small one (QWERTZ, V43): "grün" typed is "Grün".
  assert(sortEntrySame("\xC3\x84rger", "\xC3\xA4rger"));
  assert(sortEntrySame("gr\xC3\xBCn", "Gr\xC3\x9Cn") && !sortEntrySame("gr\xC3\xBCn", "gruen"));
  assert(!sortEntrySame("a", "\xC3\xA4"));
  // No name is no name.
  assert(!sortEntrySame(nullptr, nullptr) && !sortEntrySame("ABS", nullptr) && !sortEntrySame(nullptr, "ABS"));

  // The first spelling stays, with its hue, whatever the later one is and
  // wherever it comes from (the sources add the inventory first, then the
  // database, plan Festlegung 8).
  SortChoices* l = new SortChoices(); l->n = 0;
  assert(sortChoicesAdd(l, "Light Green", "90EE90", SF_DB) == 0);
  assert(sortChoicesAdd(l, "light green", "00FF00", SF_OWNED) == 0 && l->n == 1);
  assert(strcmp(l->item[0].name, "Light Green") == 0 && strcmp(l->item[0].hex, "90EE90") == 0);
  assert(l->item[0].from == SF_DB);
  // An added entry stands for no row of a source until the source says so.
  assert(l->item[0].row == -1);
  assert(sortChoicesAdd(l, "", nullptr, SF_DB) == -1 && sortChoicesFind(*l, "LIGHT-GREEN") == 0);
  assert(sortChoicesAdd(l, nullptr, nullptr, SF_DB) == -1 && l->n == 1);
  // A second name is the next index, a missing hex is an empty one.
  assert(sortChoicesAdd(l, "Black", nullptr, SF_USUAL) == 1 && l->n == 2);
  assert(strcmp(l->item[1].name, "Black") == 0 && l->item[1].hex[0] == '\0' && l->item[1].from == SF_USUAL && l->item[1].row == -1);
  assert(sortChoicesFind(*l, "black") == 1 && sortChoicesFind(*l, "Blue") == -1);
  assert(sortChoicesFind(*l, "") == -1 && sortChoicesFind(*l, nullptr) == -1);
  // A name longer than its field is cut at a character boundary: 30 capital
  // umlauts are 60 bytes, the field keeps 47, so 23 of them (46 bytes).
  std::string longName;
  for (int i = 0; i < 30; i++) longName += "\xC3\x84";
  int cut = sortChoicesAdd(l, longName.c_str(), "", SF_OWNED);
  assert(cut == 2 && strlen(l->item[2].name) == 46 && utf8Ok(l->item[2].name));
  assert(memcmp(l->item[2].name, longName.c_str(), 46) == 0 && l->item[2].row == -1);
  delete l;

  // A list holds SORT_CHOICES_MAX entries; one more is refused, one that is
  // there already is still found (and nothing changes).
  SortChoices* f = new SortChoices(); f->n = 0;
  char nm[64];
  for (int i = 0; i < SORT_CHOICES_MAX; i++) {
    snprintf(nm, sizeof(nm), "Name %d", i);
    assert(sortChoicesAdd(f, nm, "", SF_OWNED) == i);
  }
  assert(f->n == SORT_CHOICES_MAX);
  assert(sortChoicesAdd(f, "One more", "", SF_OWNED) == -1 && f->n == SORT_CHOICES_MAX);
  assert(sortChoicesAdd(f, "name-5", "", SF_DB) == 5 && f->n == SORT_CHOICES_MAX);
  assert(strcmp(f->item[5].name, "Name 5") == 0 && f->item[5].from == SF_OWNED);
  assert(sortChoicesFind(*f, "NAME 95") == 95 && sortChoicesFind(*f, "One more") == -1);
  delete f;

  // Weights: once each, 50 to 10000 g, named "850 g".
  SortChoices* g = new SortChoices(); g->n = 0;
  assert(sortChoicesAddGrams(g, 850, SF_USUAL) == 0);
  assert(strcmp(g->item[0].name, "850 g") == 0 && g->item[0].grams == 850);
  assert(g->item[0].from == SF_USUAL && g->item[0].hex[0] == '\0' && g->item[0].row == -1);
  assert(sortChoicesAddGrams(g, 850, SF_OWNED) == 0 && g->n == 1 && g->item[0].from == SF_USUAL);
  assert(sortChoicesFind(*g, "850g") == 0);
  assert(sortChoicesAddGrams(g, 49, SF_DB) == -1 && sortChoicesAddGrams(g, 10001, SF_DB) == -1);
  assert(sortChoicesAddGrams(g, 0, SF_DB) == -1 && sortChoicesAddGrams(g, -850, SF_DB) == -1 && g->n == 1);
  assert(sortChoicesAddGrams(g, 50, SF_DB) == 1 && sortChoicesAddGrams(g, 10000, SF_DB) == 2 && g->n == 3);
  assert(strcmp(g->item[1].name, "50 g") == 0 && g->item[1].grams == 50);
  assert(strcmp(g->item[2].name, "10000 g") == 0 && g->item[2].grams == 10000);
  // Filled up (3 + 93 = 96), one more weight is refused, a known one is found.
  for (int w = 100; w < 193; w++) assert(sortChoicesAddGrams(g, w, SF_DB) == w - 100 + 3);
  assert(g->n == SORT_CHOICES_MAX);
  assert(sortChoicesAddGrams(g, 200, SF_DB) == -1 && sortChoicesAddGrams(g, 850, SF_DB) == 0 && g->n == SORT_CHOICES_MAX);
  delete g;

  // Hex: six digits, a '#' in front is welcome, capitals come out.
  char hx[7];
  assert(sortEntryHex("90ee90", hx) && strcmp(hx, "90EE90") == 0 && sortEntryHex("#90EE90", hx));
  assert(strcmp(hx, "90EE90") == 0);
  assert(sortEntryHex("#90ee90", hx) && strcmp(hx, "90EE90") == 0);
  assert(sortEntryHex("000000", hx) && strcmp(hx, "000000") == 0);
  assert(!sortEntryHex("90EE9", hx) && !sortEntryHex("90EG90", hx) && !sortEntryHex("90EE900", hx) && !sortEntryHex("", hx) && !sortEntryHex(nullptr, hx));
  assert(!sortEntryHex("#", hx) && !sortEntryHex("##90EE90", hx));

  // Lines in names (SpoolmanDB): whole words, any case, the two-word one too.
  assert(strcmp(sortEntryLineOf("Matte Black"), "Matte") == 0 && strcmp(sortEntryLineOf("matte black"), "Matte") == 0);
  assert(strcmp(sortEntryLineOf("High Speed Matte Black"), "High Speed") == 0);
  assert(sortEntryLineOf("Black") == nullptr && sortEntryLineOf("Silky Gold") == nullptr);
  assert(strcmp(sortEntryLineOf("Matte-Black"), "Matte") == 0);
  assert(sortEntryLineOf("") == nullptr && sortEntryLineOf(nullptr) == nullptr);

  // Colour of an entry for the chosen line.
  char c[SORT_NAME_MAX];
  assert(sortEntryColourOf("Light Green", nullptr, "", c, sizeof(c)) && strcmp(c, "Light Green") == 0);
  assert(!sortEntryColourOf("Matte Black", nullptr, "", c, sizeof(c)));
  assert(c[0] == '\0');
  assert(sortEntryColourOf("Matte Black", nullptr, "matte", c, sizeof(c)) && strcmp(c, "Black") == 0);
  assert(!sortEntryColourOf("Silk Gold", nullptr, "Matte", c, sizeof(c)));
  assert(sortEntryColourOf("Black", "Basic", "Basic", c, sizeof(c)) && !sortEntryColourOf("Black", "Basic", "", c, sizeof(c)));
  assert(sortEntryColourOf("Black", "", "", c, sizeof(c)));
  assert(sortEntryColourOf("High Speed Matte Black", nullptr, "High Speed", c, sizeof(c)) && strcmp(c, "Matte Black") == 0);

  // Names of the inventory (Spoolman): material, then line, then colour.
  char ln[SORT_LINE_MAX], co[SORT_NAME_MAX];
  sortEntrySplitName("PETG Basic", "PETG", ln, co);            assert(!strcmp(ln, "Basic") && !strcmp(co, ""));
  sortEntrySplitName("Matte Light Green", "ABS", ln, co);      assert(!strcmp(ln, "Matte") && !strcmp(co, "Light Green"));
  sortEntrySplitName("ASA", "ASA", ln, co);                    assert(!strcmp(ln, "") && !strcmp(co, ""));
  sortEntrySplitName("PETG Rapid-Eco Dark Green", "PETG", ln, co); assert(!strcmp(ln, "") && !strcmp(co, "Rapid-Eco Dark Green"));
  sortEntrySplitName("PLA Silk Gold", "PLA", ln, co);          assert(!strcmp(ln, "Silk") && !strcmp(co, "Gold"));
  sortEntrySplitName("Black", "PLA", ln, co);                  assert(!strcmp(ln, "") && !strcmp(co, "Black"));

  // Spoolman extras are JSON text: the string without its quotes, a quote and
  // a backslash inside it unescaped; none, empty and the empty string are no text.
  char ex[SORT_NAME_MAX];
  assert(sortEntryExtraText("\"Light Green\"", ex, sizeof(ex)) && strcmp(ex, "Light Green") == 0);
  assert(sortEntryExtraText("\"a \\\"b\\\"\"", ex, sizeof(ex)) && strcmp(ex, "a \"b\"") == 0);
  assert(!sortEntryExtraText("\"\"", ex, sizeof(ex)) && ex[0] == '\0');
  strcpy(ex, "stale");
  assert(!sortEntryExtraText("", ex, sizeof(ex)) && ex[0] == '\0');
  strcpy(ex, "stale");
  assert(!sortEntryExtraText(nullptr, ex, sizeof(ex)) && ex[0] == '\0');
  // Not a JSON string, or one that lost its closing quote: nothing.
  strcpy(ex, "stale");
  assert(!sortEntryExtraText("\"abc", ex, sizeof(ex)) && ex[0] == '\0');
  strcpy(ex, "stale");
  assert(!sortEntryExtraText("123", ex, sizeof(ex)) && ex[0] == '\0');
  // A text over the buffer is cut at a character boundary: 30 capital umlauts
  // are 60 bytes, 47 fit, so 23 of them (46 bytes) stay.
  std::string umlauts = "\"";
  for (int i = 0; i < 30; i++) umlauts += "\xC3\x84";
  umlauts += "\"";
  assert(sortEntryExtraText(umlauts.c_str(), ex, sizeof(ex)));
  assert(strlen(ex) == 46 && utf8Ok(ex) && memcmp(ex, umlauts.c_str() + 1, 46) == 0);
  // Python's json.dumps writes a non-ASCII character as the u escape: the
  // UTF-8 of one to three bytes comes out. A surrogate half is '?', an escape
  // with fewer than four hex digits is no text.
  assert(sortEntryExtraText("\"Gr\\u00fcn\"", ex, sizeof(ex)) && strcmp(ex, "Gr\xC3\xBC" "n") == 0);
  assert(sortEntryExtraText("\"\\u20ac\"", ex, sizeof(ex)) && strcmp(ex, "\xE2\x82\xAC") == 0);
  assert(sortEntryExtraText("\"\\ud83d\"", ex, sizeof(ex)) && strcmp(ex, "?") == 0);
  strcpy(ex, "stale");
  assert(!sortEntryExtraText("\"\\u00f\"", ex, sizeof(ex)) && ex[0] == '\0');

  // Fiberlogy ABS light green (Spec 8 W) into the input.
  SortEntry e = fiberlogy();
  TagCreateInput in;
  memset(&in, 0x5A, sizeof(in));
  assert(sortEntryToInput(e, &in));
  assert(!strcmp(in.vendor, "Fiberlogy") && !strcmp(in.material, "ABS") && !strcmp(in.subtype, "") && !strcmp(in.product, "ABS"));
  assert(!strcmp(in.color_name, "Light Green") && !strcmp(in.color_name_en, "Light Green") && !strcmp(in.plain_line, ""));
  assert(!strcmp(in.color_hex, "90EE90") && !strcmp(in.colors_hex[0], "90EE90") && in.color_count == 1 && !in.clear);
  assert(!strcmp(in.rgba, "90EE90FF") && in.names_known && in.typed && in.net_weight_g == 850 && in.diameter_mm == 1.75f);
  assert(!in.db_id[0] && !in.article[0] && in.spool_weight_g == 0 && in.spool_type_g == 0);
  // No inventory filament picked: 0.
  assert(in.known_filament_id == 0);
  // Nothing of the old content stays: what the input does not get is empty.
  assert(!in.db_name[0] && !in.db_color_hex[0] && !in.db_color_name[0] && !in.db_line[0] && !in.db_material_key[0]);
  assert(!in.link_id[0] && in.spool_catalog_id == 0 && !in.spool_weight_default && in.color_kind == TCK_SINGLE);
  assert(in.temp_min == 0 && in.temp_max == 0 && in.db_density == 0.0f && in.db_bed_temp == 0);
  // With a line: the filament will be "Matte Light Green" (tagCreateSpoolmanName: subtype, then colour).
  strcpy(e.line, "Matte"); assert(sortEntryToInput(e, &in) && !strcmp(in.subtype, "Matte") && !strcmp(in.product, "ABS Matte"));
  strcpy(e.line, "");
  // An inventory filament picked (Festlegung 9) goes on as the known filament.
  SortEntry picked = e;
  picked.filament_id = 16;
  assert(sortEntryToInput(picked, &in) && in.known_filament_id == 16 && in.typed);
  assert(sortEntryToInput(e, &in) && in.known_filament_id == 0);

  // Clear: no hue, 00000000.
  SortEntry ce = e;
  strcpy(ce.colour, "Transparent");
  ce.hex[0] = '\0';
  ce.clear = true;
  assert(sortEntryToInput(ce, &in));
  assert(in.clear && in.color_count == 0 && !in.color_hex[0] && !in.colors_hex[0][0] && !strcmp(in.rgba, "00000000"));
  assert(!strcmp(in.color_name, "Transparent") && !strcmp(in.color_name_en, "Transparent") && in.names_known && in.typed);

  // Refused: no vendor / material / colour, hex invalid without clear, grams 49 or 10001.
  SortEntry r = e;
  r.vendor[0] = '\0';                     assert(!accepted(r));
  r = e; r.material[0] = '\0';            assert(!accepted(r));
  r = e; r.colour[0] = '\0';              assert(!accepted(r));
  r = e; strcpy(r.hex, "90EE9");          assert(!accepted(r));
  r = e; strcpy(r.hex, "90EG90");         assert(!accepted(r));
  r = e; r.hex[0] = '\0';                 assert(!accepted(r));
  r = e; r.grams = 49;                    assert(!accepted(r));
  r = e; r.grams = 10001;                 assert(!accepted(r));
  r = e; r.grams = 0;                     assert(!accepted(r));
  r = e; r.grams = 50;                    assert(accepted(r));
  r = e; r.grams = 10000;                 assert(accepted(r));
  assert(accepted(e));

  // Diameter: 2.85 stays, 3.0 and 0 become 1.75.
  r = e; r.diameter_mm = 2.85f;           assert(sortEntryToInput(r, &in) && in.diameter_mm == 2.85f);
  r = e; r.diameter_mm = 3.0f;            assert(sortEntryToInput(r, &in) && in.diameter_mm == 1.75f);
  r = e; r.diameter_mm = 0.0f;            assert(sortEntryToInput(r, &in) && in.diameter_mm == 1.75f);
  r = e; r.diameter_mm = 1.75f;           assert(sortEntryToInput(r, &in) && in.diameter_mm == 1.75f);

  // UTF-8: the entry's fields arrive terminated. A vendor of 15 x "\xC3\x84"
  // and "A" is 31 bytes, the most the field holds, and is copied unchanged.
  // (The cut of a colour name of 30 x "\xC3\x84" is tested with sortChoicesAdd.)
  r = e;
  for (int i = 0; i < 15; i++) { r.vendor[2 * i] = (char)0xC3; r.vendor[2 * i + 1] = (char)0x84; }
  r.vendor[30] = 'A'; r.vendor[31] = '\0';
  assert(strlen(r.vendor) == 31);
  assert(sortEntryToInput(r, &in) && strcmp(in.vendor, r.vendor) == 0 && utf8Ok(in.vendor));
  // A material of 16 bytes and a line of 31 give a product cut to at most 23
  // bytes the same way. The line is the subtype and stays whole (31 bytes).
  // "MMMMMMMMMMMMMMMM LLLLL" is 22 bytes and the next one is the first half
  // of a sequence, which goes.
  r = e;
  memset(r.material, 'M', 16); r.material[16] = '\0';
  memset(r.line, 0, sizeof(r.line));
  memset(r.line, 'L', 5);
  for (int i = 0; i < 13; i++) { r.line[5 + 2 * i] = (char)0xC3; r.line[6 + 2 * i] = (char)0x84; }
  assert(strlen(r.line) == 31);
  assert(sortEntryToInput(r, &in));
  assert(std::string(in.product) == std::string(16, 'M') + " LLLLL" && utf8Ok(in.product));
  assert(strcmp(in.subtype, r.line) == 0 && utf8Ok(in.subtype) && strcmp(in.material, r.material) == 0);
  // The cut falls between two characters: all 23 bytes of the text before it stay.
  memset(r.line, 0, sizeof(r.line));
  memset(r.line, 'L', 6);
  for (int i = 0; i < 12; i++) { r.line[6 + 2 * i] = (char)0xC3; r.line[7 + 2 * i] = (char)0x84; }
  assert(sortEntryToInput(r, &in));
  assert(std::string(in.product) == std::string(16, 'M') + " LLLLLL" && strlen(in.product) == 23);
  // A whole character ends exactly at the 23rd byte.
  memset(r.line, 0, sizeof(r.line));
  memset(r.line, 'L', 4);
  for (int i = 0; i < 13; i++) { r.line[4 + 2 * i] = (char)0xC3; r.line[5 + 2 * i] = (char)0x84; }
  assert(sortEntryToInput(r, &in));
  assert(std::string(in.product) == std::string(16, 'M') + " LLLL" + "\xC3\x84" && strlen(in.product) == 23 && utf8Ok(in.product));

  // Tables. Colours: 28 names in the spelling of SpoolmanDB (plan, Festlegung 2),
  // the hue as six digits, and the clear one with none.
  static const SortColour want[SORT_COMMON_COUNT] = {
    {"Black", "000000"}, {"White", "FFFFFF"}, {"Gray", "808080"}, {"Transparent", ""},
    {"Red", "FF0000"}, {"Dark Red", "8B0000"}, {"Orange", "FF8000"}, {"Yellow", "FFFF00"},
    {"Gold", "FFD700"}, {"Beige", "F5F5DC"}, {"Brown", "8B4513"}, {"Light Green", "90EE90"},
    {"Green", "00A000"}, {"Dark Green", "006400"}, {"Olive Green", "808000"}, {"Mint", "98FF98"},
    {"Turquoise", "40E0D0"}, {"Cyan", "00FFFF"}, {"Light Blue", "ADD8E6"}, {"Sky Blue", "87CEEB"},
    {"Blue", "0000FF"}, {"Navy Blue", "000080"}, {"Purple", "800080"}, {"Violet", "EE82EE"},
    {"Magenta", "FF00FF"}, {"Pink", "FFC0CB"}, {"Silver", "C0C0C0"}, {"Dark Gray", "404040"},
  };
  int clearOnes = 0;
  for (int i = 0; i < SORT_COMMON_COUNT; i++) {
    assert(SORT_COMMON[i].name && SORT_COMMON[i].hex);
    assert(strcmp(SORT_COMMON[i].name, want[i].name) == 0 && strcmp(SORT_COMMON[i].hex, want[i].hex) == 0);
    assert(strlen(SORT_COMMON[i].name) < (size_t)SORT_NAME_MAX);
    for (int j = i + 1; j < SORT_COMMON_COUNT; j++) assert(!sortEntrySame(SORT_COMMON[i].name, SORT_COMMON[j].name));
    if (SORT_COMMON[i].hex[0]) {
      assert(sortEntryHex(SORT_COMMON[i].hex, hx) && strcmp(hx, SORT_COMMON[i].hex) == 0);
    } else {
      assert(strcmp(SORT_COMMON[i].name, "Transparent") == 0);
      clearOnes++;
    }
  }
  assert(clearOnes == 1);
  // Lines: 18 different ones (plan, Festlegung 1), each found again in front of a colour.
  static const char* const wantLines[SORT_LINES_COUNT] = {
    "Basic", "Matte", "Silk", "Plus", "Pro", "HF", "High Speed", "Tough", "Lite",
    "CF", "GF", "Glow", "Marble", "Sparkle", "Galaxy", "Wood", "Metal", "Translucent",
  };
  for (int i = 0; i < SORT_LINES_COUNT; i++) {
    assert(SORT_LINES[i] && strcmp(SORT_LINES[i], wantLines[i]) == 0);
    assert(strlen(SORT_LINES[i]) < (size_t)SORT_LINE_MAX);
    for (int j = i + 1; j < SORT_LINES_COUNT; j++) assert(!sortEntrySame(SORT_LINES[i], SORT_LINES[j]));
    snprintf(nm, sizeof(nm), "%s Black", SORT_LINES[i]);
    const char* got = sortEntryLineOf(nm);
    assert(got && strcmp(got, SORT_LINES[i]) == 0);
  }
  // Sizes in grams, the full kilo first.
  static const uint16_t wantSizes[SORT_SIZES_COUNT] = {1000, 850, 750, 500, 250};
  for (int i = 0; i < SORT_SIZES_COUNT; i++) {
    assert(SORT_SIZES[i] == wantSizes[i]);
    assert(SORT_SIZES[i] >= SORT_GRAMS_MIN && SORT_SIZES[i] <= SORT_GRAMS_MAX);
  }
  // Materials, with no database: PLA first, each fits the material field.
  static const char* const wantMaterials[SORT_MATERIALS_COUNT] = {
    "PLA", "PLA+", "PETG", "ABS", "ASA", "TPU", "PA", "PC", "PCTG", "HIPS", "PVA", "PP",
  };
  for (int i = 0; i < SORT_MATERIALS_COUNT; i++) {
    assert(SORT_MATERIALS[i] && strcmp(SORT_MATERIALS[i], wantMaterials[i]) == 0);
    assert(strlen(SORT_MATERIALS[i]) < sizeof(SortEntry::material));
    for (int j = i + 1; j < SORT_MATERIALS_COUNT; j++) assert(!sortEntrySame(SORT_MATERIALS[i], SORT_MATERIALS[j]));
  }
  assert(strcmp(SORT_MATERIALS[0], "PLA") == 0);
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp),
                    str(root / "src" / "services" / "sort_entry_rules.cpp"),
                    str(root / "src" / "services" / "text_fold.cpp"),
                    str(root / "src" / "services" / "text_util.cpp"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("sort entry rules: ok")
