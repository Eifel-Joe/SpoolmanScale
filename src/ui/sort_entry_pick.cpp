#include "ui/sort_entry_internal.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/sd_logger.h"
#include "services/sort_entry_source.h"
#include "services/tag_create.h"
#include "services/text_util.h"
#include "ui/db_pick_core_popup.h"
#include "ui/tag_create_popup.h"
#include "ui/theme.h"
#include "ui/ui_common.h"
// Last: lang.h defines T(), which a template parameter of a header above
// would trip over.
#include "lang.h"

// ============================================================
//  THE COLOUR AND THE WEIGHT STEP, AND THE WAY ON TO THE CARD
//
//  The colour step offers the colours of the brand and the material in three
//  groups: what the inventory has, each with the line of its filament and
//  whatever line was chosen (plan Festlegung 9), what the maker has (the
//  database, and the inventory's colours of other materials), and the common
//  colours. A colour of the inventory is that filament: its colour name, hue,
//  line and Spoolman number go on to the card, which then finds it instead of
//  creating a second one. A colour typed is held against the list
//  (sortEntrySame, plan Festlegung 5); one the list does not have is new, and
//  the colour grid asks for its hue: one of the 28 common colours (plan
//  Festlegung 2) or six hex digits. The hue is what Spoolman finds the
//  filament by next time, so the grid says so on top.
//
//  The weight step takes the diameter (1.75 mm unless 2.85 is tapped) and
//  the net weight of a full spool, from the list or typed. Then the entry
//  becomes the card's input (sortEntryToInput()) and goes the way a database
//  entry of the picker goes (pickEntry() in db_pick_screen.cpp): the spool
//  type (BBS), the empty spool, the card. The entry stays on screen under
//  them at the weight step, so back or cancel there returns to it; a spool
//  created closes everything (showMainScreen(), deleteSpoolFlowOverlays()).
// ============================================================

// A colour row: the swatch as the picker's, the name after it, the line of
// an inventory filament at the right. Inside the tile's padding (2) and
// border (1) on either side.
#define PICK_ROW_INNER       (SORT_ROW_W - 2 * 3)
#define PICK_ROW_SWATCH      24
#define PICK_ROW_NAME_X      (PICK_ROW_SWATCH + 3 * SORT_GAP)
#define PICK_ROW_LINE_W      120
#define PICK_ROW_NAME_W      (PICK_ROW_INNER - PICK_ROW_NAME_X - SORT_GAP)
#define PICK_ROW_NAME_SHORT  (PICK_ROW_NAME_W - PICK_ROW_LINE_W - SORT_GAP)
// The colour grid: seven fields to a row in the width of the list (its
// padding of 2 off either side), the swatch over two lines of its name.
#define PICK_GRID_COLS       7
#define PICK_FIELD_W         ((SORT_BODY_W - 4 - (PICK_GRID_COLS - 1) * SORT_GAP) / PICK_GRID_COLS)
#define PICK_FIELD_SWATCH    28
#define PICK_FIELD_NAME_Y    (PICK_FIELD_SWATCH + 2)
// A clear filament on a swatch: no hue, no alpha, the way Bambu writes it
// and sortEntryToInput() files it; swatchPaint() draws it as glass.
#define PICK_CLEAR_RGBA      "00000000"
// The two diameters; one near the second is the second, as
// sortEntryToInput() reads it.
#define PICK_DIA_THIN        1.75f
#define PICK_DIA_THICK       2.85f
// What the keyboards take: a hue with its '#', "#90EE90", and the grams,
// up to "10000".
#define PICK_HEX_LEN         7
#define PICK_GRAMS_LEN       5

// The groups of the colour step, in this order, and their captions.
enum ColourGroup : uint8_t { CG_OWNED = 0, CG_MAKER, CG_COMMON, CG_COUNT };
static const int GROUP_CAPTION[CG_COUNT] = { STR_DBPICK_OWNED, STR_SORT_THIS_MAKER, STR_SORT_COMMON };

// The colour came from a filament of the inventory: its line, which goes on
// in place of the one chosen (plan Festlegung 9). Kept apart from the entry,
// so a way back from the weight step finds the chosen line unchanged.
static bool s_from_row = false;
static char s_row_line[SORT_LINE_MAX] = "";

// Shared styles: every local style property is an allocation of its own in
// the LVGL pool, and a colour list is up to SORT_CHOICES_MAX rows. Size and
// place are in them too.
static lv_style_t s_st_swatch, s_st_row_swatch, s_st_row_name, s_st_row_short, s_st_row_line;
static lv_style_t s_st_field, s_st_field_pressed, s_st_field_swatch, s_st_field_name;
static lv_style_t s_st_on, s_st_way;
static bool       s_styles_ready = false;

// One line of a label: the dots need a height to end at, or the text wraps.
static void oneLine(lv_style_t* st, const lv_font_t* font) {
  lv_style_set_text_font(st, font);
  lv_style_set_height(st, lv_font_get_line_height(font));
}

static void place(lv_style_t* st, int w, lv_align_t align, int x) {
  lv_style_set_width(st, w);
  lv_style_set_align(st, align);
  lv_style_set_x(st, x);
}

// Once, after boot: the palette is chosen at boot and fixed until a restart.
static void initStyles() {
  if (s_styles_ready) return;
  // A swatch as the picker's: a thin frame, the colour painted on it.
  lv_style_init(&s_st_swatch);
  lv_style_set_radius(&s_st_swatch, UI_RADIUS_INPUT);
  lv_style_set_border_width(&s_st_swatch, 1);
  lv_style_set_border_color(&s_st_swatch, lv_color_hex(UI_COL_POPUP_BORDER));
  lv_style_set_pad_all(&s_st_swatch, 0);
  lv_style_init(&s_st_row_swatch);
  place(&s_st_row_swatch, PICK_ROW_SWATCH, LV_ALIGN_LEFT_MID, SORT_GAP);
  lv_style_set_height(&s_st_row_swatch, PICK_ROW_SWATCH);
  lv_style_init(&s_st_row_name);
  place(&s_st_row_name, PICK_ROW_NAME_W, LV_ALIGN_LEFT_MID, PICK_ROW_NAME_X);
  oneLine(&s_st_row_name, UI_FONT_BODY);
  lv_style_set_text_color(&s_st_row_name, lv_color_hex(UI_COL_INK));
  // The name beside the line of an inventory filament.
  lv_style_init(&s_st_row_short);
  lv_style_set_width(&s_st_row_short, PICK_ROW_NAME_SHORT);
  lv_style_init(&s_st_row_line);
  place(&s_st_row_line, PICK_ROW_LINE_W, LV_ALIGN_RIGHT_MID, -SORT_GAP);
  oneLine(&s_st_row_line, UI_FONT_CAPTION);
  lv_style_set_text_color(&s_st_row_line, lv_color_hex(UI_COL_INK_SOFT));
  lv_style_set_text_align(&s_st_row_line, LV_TEXT_ALIGN_RIGHT);
  // A field of the grid has no frame of its own: the swatch is the frame,
  // the name under it, the field shows the press.
  lv_style_init(&s_st_field);
  lv_style_set_width(&s_st_field, PICK_FIELD_W);
  lv_style_set_height(&s_st_field, PICK_FIELD_NAME_Y + 2 * lv_font_get_line_height(UI_FONT_CAPTION));
  lv_style_set_bg_opa(&s_st_field, LV_OPA_TRANSP);
  lv_style_set_border_width(&s_st_field, 0);
  lv_style_set_radius(&s_st_field, UI_RADIUS_INPUT);
  lv_style_set_pad_all(&s_st_field, 0);
  lv_style_init(&s_st_field_pressed);
  lv_style_set_bg_opa(&s_st_field_pressed, LV_OPA_COVER);
  lv_style_set_bg_color(&s_st_field_pressed, lv_color_hex(UI_COL_PRESS_FILL));
  lv_style_init(&s_st_field_swatch);
  lv_style_set_width(&s_st_field_swatch, PICK_FIELD_W);
  lv_style_set_height(&s_st_field_swatch, PICK_FIELD_SWATCH);
  lv_style_init(&s_st_field_name);
  lv_style_set_width(&s_st_field_name, PICK_FIELD_W);
  lv_style_set_y(&s_st_field_name, PICK_FIELD_NAME_Y);
  lv_style_set_text_font(&s_st_field_name, UI_FONT_CAPTION);
  lv_style_set_text_color(&s_st_field_name, lv_color_hex(UI_COL_INK));
  lv_style_set_text_align(&s_st_field_name, LV_TEXT_ALIGN_CENTER);
  // The diameter chosen: the frame the maker step gives the inventory's brands.
  lv_style_init(&s_st_on);
  lv_style_set_border_width(&s_st_on, 2);
  lv_style_set_border_color(&s_st_on, lv_color_hex(UI_COL_ACCENT));
  // A way to the keyboard reads as a way, not as one more value.
  lv_style_init(&s_st_way);
  lv_style_set_text_color(&s_st_way, lv_color_hex(UI_COL_ACCENT));
  s_styles_ready = true;
}

// The name a typed one matched, for the top of the next step; null for none.
static void noteExists(const char* name) {
  SortUi& ui = sortUi();
  if (name) utf8Cut(name, sizeof(ui.exists) - 1, ui.exists, sizeof(ui.exists));
  else      ui.exists[0] = '\0';
}

// A swatch with a colour of the lists, "RRGGBB", or glass for the empty hex
// of a clear filament, as the picker draws a clear one.
static lv_obj_t* swatch(lv_obj_t* parent, const char* hex, lv_style_t* where) {
  lv_obj_t* sw = lv_obj_create(parent);
  lv_obj_add_style(sw, &s_st_swatch, 0);
  lv_obj_add_style(sw, where, 0);
  lv_obj_clear_flag(sw, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);
  swatchPaintHex(sw, (hex && hex[0]) ? hex : PICK_CLEAR_RGBA);
  return sw;
}

// The hue of the entry: "RRGGBB", or clear for none.
static void takeHue(SortEntry& e, const char* hex) {
  snprintf(e.hex, sizeof(e.hex), "%s", hex ? hex : "");
  e.clear = !e.hex[0];
}

static bool thick(const SortEntry& e) {
  return e.diameter_mm > 2.80f && e.diameter_mm < 2.90f;
}

// ------------------------------------------------------------
//  Step 4: the colour
// ------------------------------------------------------------

static uint8_t groupOf(const SortChoice& c) {
  if (c.row >= 0) return CG_OWNED;
  return c.from == SF_USUAL ? CG_COMMON : CG_MAKER;
}

static void colourRow(lv_obj_t* body, const SortData& d, int i) {
  const SortChoice& c = d.colours.item[i];
  lv_obj_t* row = sortRow(body, SN_COLOUR, i);
  swatch(row, c.hex, &s_st_row_swatch);
  lv_obj_t* name = lv_label_create(row);
  lv_label_set_text(name, c.name);
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  lv_obj_add_style(name, &s_st_row_name, 0);
  const char* line = (c.row >= 0 && c.row < d.row_n) ? d.rows[c.row].line : "";
  if (!line[0]) return;
  lv_obj_add_style(name, &s_st_row_short, 0);
  lv_obj_t* l = lv_label_create(row);
  lv_label_set_text(l, line);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_add_style(l, &s_st_row_line, 0);
}

void sortShowColours() {
  SortUi& ui = sortUi();
  const SortData* d = sortData();
  initStyles();
  ui.step = SS_COLOUR;
  // "Fiberlogy ABS Basic: Farbe", the way the picker titles its colours.
  char what[sizeof(ui.entry.material) + sizeof(ui.entry.line) + 1];
  snprintf(what, sizeof(what), "%s%s%s", ui.entry.material, ui.entry.line[0] ? " " : "", ui.entry.line);
  char title[sizeof(ui.entry.vendor) + sizeof(what) + 32];
  snprintf(title, sizeof(title), T(STR_SORT_COLOUR_TITLE), ui.entry.vendor, what);
  sortTitle(title);
  lv_obj_t* body = sortNewBody();
  logLvMem("sortentry-colours/pre", 0);
  sortExistsCaption(body);
  int rows = 0;
  bool full = false;
  for (uint8_t g = 0; g < CG_COUNT && !full && d; g++) {
    bool captioned = false;
    for (int i = 0; i < d->colours.n; i++) {
      if (groupOf(d->colours.item[i]) != g) continue;
      if (!lvPoolHasRoomForRow()) { logSDf("SortEntry: LVGL pool low, colours cut at %d", rows); full = true; break; }
      if (!captioned) sortCaption(body, T(GROUP_CAPTION[g]));
      captioned = true;
      colourRow(body, *d, i);
      rows++;
    }
  }
  sortNewTile(body);
  logLvMem("sortentry-colours/post", rows);
}

// A colour of the list, tapped or typed: the weight step next.
static void chooseColour(int k) {
  const SortData* d = sortData();
  if (!d || k < 0 || k >= d->colours.n) return;
  const SortChoice& c = d->colours.item[k];
  SortEntry& e = sortUi().entry;
  s_from_row = c.row >= 0 && c.row < d->row_n;
  if (s_from_row) {
    // That filament, under the colour name of its spool (plan Festlegung 9).
    const SortRow& r = d->rows[c.row];
    utf8Cut(r.name, sizeof(e.colour) - 1, e.colour, sizeof(e.colour));
    takeHue(e, r.hex);
    utf8Cut(r.line, sizeof(s_row_line) - 1, s_row_line, sizeof(s_row_line));
    e.filament_id = r.filament_id;
  } else {
    utf8Cut(c.name, sizeof(e.colour) - 1, e.colour, sizeof(e.colour));
    takeHue(e, c.hex);
    e.filament_id = 0;
  }
  logSDf("SortEntry: colour %s, %s%s, filament %d", e.colour, e.clear ? "clear" : e.hex,
         s_from_row ? " (inventory)" : "", e.filament_id);
  sortShowWeights();
}

// ------------------------------------------------------------
//  The colour grid: the hue of a colour typed new
// ------------------------------------------------------------

static void gridField(lv_obj_t* body, int i) {
  lv_obj_t* f = lv_obj_create(body);
  lv_obj_add_style(f, &s_st_field, 0);
  lv_obj_add_style(f, &s_st_field_pressed, LV_STATE_PRESSED);
  lv_obj_clear_flag(f, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(f, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_add_event_cb(f, sortOnNav, LV_EVENT_CLICKED, sortNavData(SN_SWATCH, i));
  swatch(f, SORT_COMMON[i].hex, &s_st_field_swatch);
  lv_obj_t* l = lv_label_create(f);
  lv_label_set_text(l, SORT_COMMON[i].name);
  lv_obj_add_style(l, &s_st_field_name, 0);
  // Two lines where the name has a blank ("Light Green"); a single word
  // wider than the field ends in dots ("Transparent") rather than breaking.
  if (!strchr(SORT_COMMON[i].name, ' ')) {
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_height(l, lv_font_get_line_height(UI_FONT_CAPTION));
  }
}

static void showGrid() {
  SortUi& ui = sortUi();
  initStyles();
  ui.step = SS_RASTER;
  char title[sizeof(ui.entry.colour) + 48];
  snprintf(title, sizeof(title), T(STR_SORT_RASTER_TITLE), ui.entry.colour);
  sortTitle(title);
  lv_obj_t* body = sortNewBody();
  logLvMem("sortentry-grid/pre", 0);
  sortCaption(body, T(STR_SORT_HUE_HINT));
  int fields = 0;
  for (int i = 0; i < SORT_COMMON_COUNT; i++) {
    if (!lvPoolHasRoomForRow()) { logSDf("SortEntry: LVGL pool low, colour grid cut at %d", i); break; }
    gridField(body, i);
    fields++;
  }
  if (lvPoolHasRoomForRow()) {
    lv_obj_t* t = sortTile(body, T(STR_SORT_HEX), SN_HEX, 0);
    lv_obj_set_width(t, SORT_ROW_W);
    lv_obj_add_style(t, &s_st_way, 0);
  }
  logLvMem("sortentry-grid/post", fields);
}

// A hue for the colour typed new: a field of the grid or six hex digits.
static void newHue(const char* hex) {
  SortEntry& e = sortUi().entry;
  takeHue(e, hex);
  e.filament_id = 0;
  s_from_row = false;
  logSDf("SortEntry: new colour %s, %s", e.colour, e.clear ? "clear" : e.hex);
  sortShowWeights();
}

static void hexTyped(const char* text) {
  char hex[7];
  if (!sortEntryHex(text, hex)) {
    sortKeysSay(T(STR_SORT_HEX_BAD));
    return;
  }
  newHue(hex);
}

void sortColourTyped(const char* text) {
  const SortData* d = sortData();
  const int k = d ? sortChoicesFind(d->colours, text) : -1;
  if (k >= 0) {
    logSDf("SortEntry: typed colour is %s", d->colours.item[k].name);
    noteExists(d->colours.item[k].name);
    chooseColour(k);
    return;
  }
  // New: the name as typed, the hue from the grid.
  SortEntry& e = sortUi().entry;
  utf8Cut(text, sizeof(e.colour) - 1, e.colour, sizeof(e.colour));
  e.hex[0] = '\0';
  e.clear = false;
  e.filament_id = 0;
  s_from_row = false;
  showGrid();
}

// ------------------------------------------------------------
//  Step 5: diameter and weight
// ------------------------------------------------------------

void sortShowWeights() {
  SortUi& ui = sortUi();
  const SortData* d = sortData();
  initStyles();
  ui.step = SS_WEIGHT;
  sortTitle(T(STR_SORT_WEIGHT_TITLE));
  lv_obj_t* body = sortNewBody();
  logLvMem("sortentry-weights/pre", 0);
  sortExistsCaption(body);
  sortCaption(body, T(STR_W_TAG_DIA));
  int tiles = 0;
  for (int k = 0; k < 2; k++) {
    if (!lvPoolHasRoomForRow()) { logSD("SortEntry: LVGL pool low, no diameter tiles"); break; }
    char text[12];
    snprintf(text, sizeof(text), "%.2f mm", k ? PICK_DIA_THICK : PICK_DIA_THIN);
    lv_obj_t* t = sortTile(body, text, SN_DIAMETER, k);
    if ((k == 1) == thick(ui.entry)) lv_obj_add_style(t, &s_st_on, 0);
    tiles++;
  }
  // The weights start a row of their own under the diameters, "Type in" last.
  bool first = true;
  const int n = d ? d->weights.n : 0;
  for (int i = 0; i <= n; i++) {
    if (!lvPoolHasRoomForRow()) { logSDf("SortEntry: LVGL pool low, weights cut at %d", i); break; }
    lv_obj_t* t = i < n ? sortTile(body, d->weights.item[i].name, SN_WEIGHT, i)
                        : sortTile(body, T(STR_SORT_TYPE_GRAMS), SN_GRAMS, 0);
    if (i == n) lv_obj_add_style(t, &s_st_way, 0);
    if (first) lv_obj_add_flag(t, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    first = false;
    tiles++;
  }
  logLvMem("sortentry-weights/post", tiles);
}

// The entry as the card's input, then the way a database entry of the
// picker goes on: the spool type (BBS), the empty spool, the card. The entry
// stays under them at this step.
static void handOver() {
  SortUi& ui = sortUi();
  SortEntry e = ui.entry;
  if (s_from_row) utf8Cut(s_row_line, sizeof(e.line) - 1, e.line, sizeof(e.line));
  TagCreateInput in;
  if (!sortEntryToInput(e, &in)) {
    // The steps hand over nothing it refuses; should it happen all the same,
    // it is logged and said in place of the list.
    logSDf("SortEntry: entry refused: vendor '%s', material '%s', colour '%s', hex '%s', clear %d, %d g",
           e.vendor, e.material, e.colour, e.hex, (int)e.clear, e.grams);
    sortMessage(T(STR_BAG_INVALID));
    return;
  }
  utf8Cut(e.vendor, sizeof(ui.last_vendor) - 1, ui.last_vendor, sizeof(ui.last_vendor));
  utf8Cut(e.material, sizeof(ui.last_material) - 1, ui.last_material, sizeof(ui.last_material));
  logSDf("SortEntry: hand over %s %s, line '%s', colour '%s' %s, %d g, %.2f mm, filament %d", in.vendor,
         in.material, in.subtype, in.color_name, in.clear ? "clear" : in.color_hex, in.net_weight_g,
         in.diameter_mm, e.filament_id);
  if (dbPickTypeAsk(&in)) return;
  if (dbPickCoreAsk(&in)) return;
  showTagCreatePopupFor(in);
}

static void gramsTyped(const char* text) {
  // Digits only (SK_GRAMS), five at the most.
  const long grams = strtol(text, nullptr, 10);
  if (grams < SORT_GRAMS_MIN || grams > SORT_GRAMS_MAX) {
    sortKeysSay(T(STR_SORT_GRAMS_BAD));
    return;
  }
  sortUi().entry.grams = (int)grams;
  handOver();
}

// ------------------------------------------------------------
//  Taps
// ------------------------------------------------------------

bool sortPickNav(uint8_t nav, int arg) {
  SortUi& ui = sortUi();
  const SortData* d = sortData();
  switch (nav) {
    case SN_COLOUR:
      if (ui.step == SS_COLOUR && d && arg < d->colours.n) {
        noteExists(nullptr);
        chooseColour(arg);
      }
      return true;
    case SN_SWATCH:
      if (ui.step == SS_RASTER && arg < SORT_COMMON_COUNT) newHue(SORT_COMMON[arg].hex);
      return true;
    case SN_HEX:
      if (ui.step == SS_RASTER) sortKeysShow(SK_HEX, STR_SORT_KEY_HEX, PICK_HEX_LEN, hexTyped);
      return true;
    case SN_DIAMETER:
      if (ui.step == SS_WEIGHT) {
        ui.entry.diameter_mm = arg ? PICK_DIA_THICK : PICK_DIA_THIN;
        sortShowWeights();
      }
      return true;
    case SN_WEIGHT:
      if (ui.step == SS_WEIGHT && d && arg < d->weights.n) {
        ui.entry.grams = d->weights.item[arg].grams;
        handOver();
      }
      return true;
    case SN_GRAMS:
      if (ui.step == SS_WEIGHT) sortKeysShow(SK_GRAMS, STR_SORT_KEY_GRAMS, PICK_GRAMS_LEN, gramsTyped);
      return true;
    default:
      return false;
  }
}
