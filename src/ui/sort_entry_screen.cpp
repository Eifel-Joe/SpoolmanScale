#include "ui/sort_entry_screen.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "app/app_state.h"
#include "hardware/sd_logger.h"
#include "services/backend.h"
#include "services/filament_db.h"
#include "services/no_tag_create_rules.h"
#include "services/sort_entry_source.h"
#include "services/text_util.h"
#include "ui/db_pick_screen.h"
#include "ui/link_wait_card.h"
#include "ui/no_tag_create.h"
#include "ui/sort_entry_internal.h"
#include "ui/spool_flow.h"
#include "ui/theme.h"
#include "ui/ui_common.h"
// Last: lang.h defines T(), which a template parameter of a header above
// would trip over.
#include "lang.h"

// Inside a tile: little, so "REDLINE FILAMENT" fits a third of the width.
#define SORT_TILE_PAD       2
#define SORT_HEAD_BTN       44
// The letters above the makers.
#define SORT_LETTER_H       36
#define SORT_LETTER_W       32
#define SORT_LETTERS        27     // one for digits and the rest, then A-Z
// The most tiles one view of the maker step shows: the inventory's brands
// (SORT_CHOICES_MAX of them at most), or the makers of one letter
// (SpoolmanDB: 13 under "S", 08.10.2026).
#define SORT_VIEW_TILES     128
// Marks a brand of the inventory among the makers of the index in one view.
#define SORT_BRAND_BIT      0x8000
// An index load refused while filament_db was free (heap, task) is asked for
// again this much later, as the picker does (DBP_RETRY_MS).
#define SORT_INDEX_RETRY_MS 2000

// The index load the entry waits for at its open.
enum SortIndex : uint8_t { IDX_NONE = 0, IDX_WANT, IDX_RUNNING };

static lv_obj_t* s_scr   = nullptr;
static lv_obj_t* s_title = nullptr;
static lv_obj_t* s_body  = nullptr;
static lv_obj_t* s_strip = nullptr;   // the letters, on the maker step only
static lv_obj_t* s_letter_btn[SORT_LETTERS];
static int       s_letter = -1;       // the letter whose makers are shown; -1: the overview
static SortUi    s_ui;
static uint8_t   s_nav = SN_NONE;
static int       s_nav_arg = 0;
// The maker of the picker list the way in was drawn on; empty on its maker list.
static char      s_pick_maker[32] = "";
// The maker to open at once the index is in; empty for the maker step.
static char      s_open_maker[32] = "";
static SortIndex s_index = IDX_NONE;
static uint32_t  s_index_ms = 0;      // when a start was refused while idle, 0 for never
static bool      s_index_again = false;   // the load vanished once and was started again

// Shared styles, as in the picker: every local style property is an
// allocation of its own in the LVGL pool, and a maker list is a hundred tiles.
static lv_style_t s_st_tile, s_st_tile_pressed, s_st_owned, s_st_letter, s_st_letter_on;
static lv_style_t s_st_text, s_st_new;
static bool       s_styles_ready = false;

// Once, after boot: the palette is chosen at boot and fixed until a restart.
static void initStyles() {
  if (s_styles_ready) return;
  lv_style_init(&s_st_tile);
  lv_style_set_bg_opa(&s_st_tile, LV_OPA_COVER);
  lv_style_set_bg_color(&s_st_tile, lv_color_hex(UI_COL_SURFACE));
  lv_style_set_radius(&s_st_tile, UI_RADIUS_BTN);
  lv_style_set_shadow_width(&s_st_tile, 0);
  lv_style_set_border_width(&s_st_tile, 1);
  lv_style_set_border_color(&s_st_tile, lv_color_hex(UI_COL_LINE));
  lv_style_set_pad_all(&s_st_tile, SORT_TILE_PAD);
  lv_style_init(&s_st_tile_pressed);
  lv_style_set_bg_color(&s_st_tile_pressed, lv_color_hex(UI_COL_PRESS_FILL));
  // A brand of the inventory has the frame the picker gives its owned makers.
  lv_style_init(&s_st_owned);
  lv_style_set_border_color(&s_st_owned, lv_color_hex(UI_COL_ACCENT));
  lv_style_init(&s_st_letter);
  lv_style_set_bg_opa(&s_st_letter, LV_OPA_COVER);
  lv_style_set_bg_color(&s_st_letter, lv_color_hex(UI_COL_ROW));
  lv_style_set_radius(&s_st_letter, UI_RADIUS_INPUT);
  lv_style_set_shadow_width(&s_st_letter, 0);
  lv_style_init(&s_st_letter_on);
  lv_style_set_border_width(&s_st_letter_on, 2);
  lv_style_set_border_color(&s_st_letter_on, lv_color_hex(UI_COL_ACCENT));
  lv_style_init(&s_st_text);
  lv_style_set_text_font(&s_st_text, UI_FONT_SMALL);
  lv_style_set_text_color(&s_st_text, lv_color_hex(UI_COL_INK));
  // The way to the keyboard reads as a way, not as one more name.
  lv_style_init(&s_st_new);
  lv_style_set_text_color(&s_st_new, lv_color_hex(UI_COL_ACCENT));
  s_styles_ready = true;
}

SortUi& sortUi() { return s_ui; }

lv_obj_t* sortEntryScreen() { return s_scr; }

bool sortEntryOffered() {
  // The flag stands in for the id here too (X), see dbPickOffered().
  return !backendIsFilaMan() &&
         noTagSourceOffered(noTagMode(), strlen(g_tag.tray_uuid) == 32, link_tag_uid[0] != '\0');
}

// ------------------------------------------------------------
//  Taps: parked for the loop
// ------------------------------------------------------------

void sortPark(uint8_t nav, int arg) {
  s_nav = nav;
  s_nav_arg = arg;
}

void* sortNavData(uint8_t nav, int arg) {
  return (void*)(intptr_t)(((intptr_t)nav << 16) | (arg & 0xFFFF));
}

void sortOnNav(lv_event_t* e) {
  const intptr_t v = (intptr_t)lv_event_get_user_data(e);
  sortPark((uint8_t)(v >> 16), (int)(v & 0xFFFF));
}

void sortEntryEntryTap(lv_event_t*) {
  logSD("BTN: Enter filament");
  sortPark(SN_OPEN, 0);
}

static void onPickTap(lv_event_t* e) {
  logSD("BTN: Enter filament (picker)");
  sortOnNav(e);
}

// ------------------------------------------------------------
//  The frame
// ------------------------------------------------------------

static lv_obj_t* headButton(lv_obj_t* head, bool close) {
  lv_obj_t* b = lv_btn_create(head);
  lv_obj_set_size(b, SORT_HEAD_BTN, SORT_HEAD_BTN);
  if (close) lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -SORT_GAP, SORT_GAP);
  else       lv_obj_set_pos(b, SORT_GAP, SORT_GAP);
  lv_obj_set_style_bg_color(b, lv_color_hex(close ? UI_COL_BAD_BG : UI_COL_SURFACE), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(close ? UI_COL_BAD_BG_PRESSED : UI_COL_PRESS_FILL),
                            LV_STATE_PRESSED);
  lv_obj_set_style_radius(b, UI_RADIUS_BTN, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_add_event_cb(b, sortOnNav, LV_EVENT_CLICKED, sortNavData(close ? SN_CLOSE : SN_BACK, 0));
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, close ? LV_SYMBOL_CLOSE : LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(l, lv_color_hex(close ? UI_COL_BAD_TEXT : UI_COL_ACCENT), 0);
  lv_obj_set_style_text_font(l, UI_FONT_TITLE, 0);
  lv_obj_center(l);
  return b;
}

static void buildFrame() {
  s_scr = lv_obj_create(lv_scr_act());
  lv_obj_set_size(s_scr, LV_HOR_RES, LV_VER_RES);
  lv_obj_set_pos(s_scr, 0, 0);
  lv_obj_set_style_bg_color(s_scr, lv_color_hex(UI_COL_GROUND), 0);
  lv_obj_set_style_border_width(s_scr, 0, 0);
  lv_obj_set_style_radius(s_scr, 0, 0);
  lv_obj_set_style_pad_all(s_scr, 0, 0);
  lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

  headButton(s_scr, false);
  headButton(s_scr, true);
  s_title = lv_label_create(s_scr);
  lv_obj_set_style_text_color(s_title, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_text_font(s_title, UI_FONT_BODY, 0);
  lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(s_title, LV_HOR_RES - 2 * (SORT_HEAD_BTN + 2 * SORT_GAP));
  lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, (SORT_HEAD_H - 20) / 2);

  lv_obj_t* rule = lv_obj_create(s_scr);
  lv_obj_set_size(rule, LV_HOR_RES, 1);
  lv_obj_set_pos(rule, 0, SORT_HEAD_H);
  lv_obj_set_style_bg_color(rule, lv_color_hex(UI_COL_LINE), 0);
  lv_obj_set_style_border_width(rule, 0, 0);
  lv_obj_set_style_radius(rule, 0, 0);
  lv_obj_set_style_pad_all(rule, 0, 0);
}

// A scrolling container under the header, or under the letters.
static lv_obj_t* makeBody(bool under_letters) {
  const int y = SORT_BODY_Y + (under_letters ? SORT_LETTER_H : 0);
  s_body = lv_obj_create(s_scr);
  lv_obj_set_size(s_body, SORT_BODY_W, SORT_BODY_H - (y - SORT_BODY_Y));
  lv_obj_set_pos(s_body, (LV_HOR_RES - SORT_BODY_W) / 2, y);
  lv_obj_set_style_bg_opa(s_body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_body, 0, 0);
  lv_obj_set_style_radius(s_body, 0, 0);
  lv_obj_set_style_pad_all(s_body, 2, 0);
  lv_obj_set_style_pad_row(s_body, SORT_GAP, 0);
  lv_obj_set_style_pad_column(s_body, SORT_GAP, 0);
  lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_scroll_dir(s_body, LV_DIR_VER);
  return s_body;
}

// The same in place of what the screen showed, the letters included.
static lv_obj_t* newBody(bool under_letters) {
  if (s_body) lv_obj_del(s_body);
  if (s_strip) lv_obj_del(s_strip);
  s_strip = nullptr;
  memset(s_letter_btn, 0, sizeof(s_letter_btn));
  return makeBody(under_letters);
}

lv_obj_t* sortNewBody() { return newBody(false); }

void sortTitle(const char* text) {
  if (s_title) lv_label_set_text(s_title, text);
}

void sortCaption(lv_obj_t* parent, const char* text) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_width(l, SORT_ROW_W);
  lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_CAPTION), 0);
  lv_obj_set_style_text_font(l, UI_FONT_SMALL, 0);
  lv_obj_set_style_pad_top(l, SORT_GAP, 0);
}

void sortMessage(const char* text) {
  lv_obj_t* body = newBody(false);
  lv_obj_t* l = lv_label_create(body);
  lv_label_set_text(l, text);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, SORT_ROW_W);
  lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_INK_SOFT), 0);
  lv_obj_set_style_text_font(l, UI_FONT_BODY, 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_top(l, SORT_BODY_H / 3, 0);
}

// A tile that is one label: background, border and text in one object,
// where a button with a label in it is two.
static lv_obj_t* labelTile(lv_obj_t* parent, int w, int h, const char* text, lv_style_t* look) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_size(l, w, h);
  lv_obj_add_style(l, look, 0);
  lv_obj_add_style(l, &s_st_tile_pressed, LV_STATE_PRESSED);
  lv_obj_add_style(l, &s_st_text, 0);
  // One line, centred: what the line does not fill is padding above and below.
  const int line_h = lv_font_get_line_height(UI_FONT_SMALL);
  lv_obj_set_style_pad_top(l, (h - line_h) / 2, 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_add_flag(l, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(l, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  return l;
}

lv_obj_t* sortTile(lv_obj_t* parent, const char* text, uint8_t nav, int arg) {
  lv_obj_t* t = labelTile(parent, SORT_TILE_W, SORT_TILE_H, text, &s_st_tile);
  lv_obj_add_event_cb(t, sortOnNav, LV_EVENT_CLICKED, sortNavData(nav, arg));
  return t;
}

lv_obj_t* sortRow(lv_obj_t* parent, uint8_t nav, int arg) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_size(b, SORT_ROW_W, SORT_ROW_H);
  lv_obj_add_style(b, &s_st_tile, 0);
  lv_obj_add_style(b, &s_st_tile_pressed, LV_STATE_PRESSED);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_add_event_cb(b, sortOnNav, LV_EVENT_CLICKED, sortNavData(nav, arg));
  return b;
}

void sortNewTile(lv_obj_t* parent) {
  if (!lvPoolHasRoomForRow()) { logSD("SortEntry: LVGL pool low, no tile for a new entry"); return; }
  lv_obj_t* t = labelTile(parent, SORT_ROW_W, SORT_TILE_H, T(STR_SORT_NEW), &s_st_tile);
  lv_obj_add_style(t, &s_st_new, 0);
  lv_obj_add_event_cb(t, sortOnNav, LV_EVENT_CLICKED, sortNavData(SN_NEW, 0));
}

void sortWaitShow() {
  linkWaitCardShow();
  linkWaitCardTitle(T(STR_DBPICK_LOADING));
  // Drawn now: the loop is about to stand still for up to 8 s.
  lv_refr_now(NULL);
}

bool sortEntryPickTile(lv_obj_t* parent, int width, const char* maker) {
  if (!parent || !sortEntryOffered()) return false;
  // True all the same: the picker draws no caption in its place either.
  if (!lvPoolHasRoomForRow()) { logSD("SortEntry: LVGL pool low, no way in on the picker"); return true; }
  initStyles();
  if (maker) utf8Cut(maker, sizeof(s_pick_maker) - 1, s_pick_maker, sizeof(s_pick_maker));
  else       s_pick_maker[0] = '\0';
  lv_obj_t* t = labelTile(parent, width, SORT_TILE_H, T(STR_SORT_NOT_LISTED), &s_st_tile);
  lv_obj_add_style(t, &s_st_new, 0);
  lv_obj_add_event_cb(t, onPickTap, LV_EVENT_CLICKED, sortNavData(SN_OPEN, maker ? 1 : 0));
  return true;
}

// ------------------------------------------------------------
//  Step 1: the maker
//
//  The brands of the inventory and the makers of the database index, a
//  letter at a time: the overview holds the maker chosen last and the
//  inventory's brands, a letter its makers and brands. A maker of the index
//  that the inventory has (sortEntrySame) is shown once, as the inventory's
//  brand in its spelling (plan Festlegung 8).
// ------------------------------------------------------------

// 0 for a name that starts with a digit or a sign, as "3D-Fuel": those sort
// first. Then A-Z as 1-26, as in the picker.
static int letterOf(const char* name) {
  const char c = name[0];
  if (c >= 'a' && c <= 'z') return 1 + c - 'a';
  if (c >= 'A' && c <= 'Z') return 1 + c - 'A';
  return 0;
}

static int makerCount() { return fdbIndexReady() ? fdbMakerCount() : 0; }

static bool makerShown(int i) {
  const FdbMaker* m = fdbMaker(i);
  const SortData* d = sortData();
  return m && !(d && sortChoicesFind(d->brands, m->name) >= 0);
}

static int brandCount() { return sortData() ? sortData()->brands.n : 0; }

static const char* viewName(uint16_t v) {
  if (v & SORT_BRAND_BIT) return sortData()->brands.item[v & ~SORT_BRAND_BIT].name;
  return fdbMaker(v)->name;
}

// The tiles of one view, A-Z whatever the case: the inventory's brands
// (letter -1), or the brands and makers of one letter.
static int viewTiles(int letter, uint16_t* out, int out_max) {
  int n = 0;
  for (int i = 0; i < brandCount() && n < out_max; i++)
    if (letter < 0 || letterOf(sortData()->brands.item[i].name) == letter) out[n++] = (uint16_t)(SORT_BRAND_BIT | i);
  for (int i = 0; letter >= 0 && i < makerCount() && n < out_max; i++)
    if (makerShown(i) && letterOf(fdbMaker(i)->name) == letter) out[n++] = (uint16_t)i;
  for (int a = 1; a < n; a++) {
    for (int b = a; b > 0 && strcasecmp(viewName(out[b - 1]), viewName(out[b])) > 0; b--) {
      const uint16_t v = out[b]; out[b] = out[b - 1]; out[b - 1] = v;
    }
  }
  return n;
}

static int makerTiles(lv_obj_t* body, int letter) {
  uint16_t view[SORT_VIEW_TILES];
  const int n = viewTiles(letter, view, SORT_VIEW_TILES);
  for (int k = 0; k < n; k++) {
    if (!lvPoolHasRoomForRow()) { logSDf("SortEntry: LVGL pool low, makers cut at %d", k); return k; }
    const bool brand = view[k] & SORT_BRAND_BIT;
    lv_obj_t* t = sortTile(body, viewName(view[k]), brand ? SN_BRAND : SN_MAKER, view[k] & ~SORT_BRAND_BIT);
    if (brand) lv_obj_add_style(t, &s_st_owned, 0);
  }
  return n;
}

// Every letter that leads a brand or a shown maker. Returns how many.
static int lettersInUse(bool used[SORT_LETTERS]) {
  memset(used, 0, sizeof(bool) * SORT_LETTERS);
  for (int i = 0; i < brandCount(); i++) used[letterOf(sortData()->brands.item[i].name)] = true;
  for (int i = 0; i < makerCount(); i++)
    if (makerShown(i)) used[letterOf(fdbMaker(i)->name)] = true;
  int n = 0;
  for (int i = 0; i < SORT_LETTERS; i++) n += used[i];
  return n;
}

// The letter whose makers are shown has a frame. Taken off first: a style
// added twice is listed twice.
static void markLetter() {
  for (int i = 0; i < SORT_LETTERS; i++) {
    if (!s_letter_btn[i]) continue;
    lv_obj_remove_style(s_letter_btn[i], &s_st_letter_on, 0);
    if (i == s_letter) lv_obj_add_style(s_letter_btn[i], &s_st_letter_on, 0);
  }
}

static void letterStrip(const bool used[SORT_LETTERS]) {
  lv_obj_t* strip = lv_obj_create(s_scr);
  s_strip = strip;
  lv_obj_set_size(strip, SORT_BODY_W, SORT_LETTER_H);
  lv_obj_set_pos(strip, (LV_HOR_RES - SORT_BODY_W) / 2, SORT_BODY_Y);
  lv_obj_set_style_bg_opa(strip, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(strip, 0, 0);
  lv_obj_set_style_pad_all(strip, 0, 0);
  lv_obj_set_style_pad_column(strip, 2, 0);
  lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
  lv_obj_set_scroll_dir(strip, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(strip, LV_SCROLLBAR_MODE_OFF);
  for (int i = 0; i < SORT_LETTERS; i++) {
    if (!used[i]) continue;
    char text[2] = { i == 0 ? '#' : (char)('A' + i - 1), '\0' };
    lv_obj_t* b = labelTile(strip, SORT_LETTER_W, SORT_LETTER_H - 4, text, &s_st_letter);
    lv_obj_add_event_cb(b, sortOnNav, LV_EVENT_CLICKED, sortNavData(SN_LETTER, i));
    s_letter_btn[i] = b;
  }
  markLetter();
}

static int fillMakers(lv_obj_t* body, bool letters) {
  int tiles = 0;
  if (s_letter >= 0) {
    tiles = makerTiles(body, s_letter);
  } else {
    if (s_ui.last_vendor[0] && !lvPoolHasRoomForRow()) {
      logSD("SortEntry: LVGL pool low, no tile for the last maker");
    } else if (s_ui.last_vendor[0]) {
      sortCaption(body, T(STR_SORT_LAST));
      lv_obj_t* t = sortTile(body, s_ui.last_vendor, SN_LAST, 0);
      if (sortData() && sortChoicesFind(sortData()->brands, s_ui.last_vendor) >= 0) lv_obj_add_style(t, &s_st_owned, 0);
      tiles++;
    }
    if (brandCount() > 0) {
      sortCaption(body, T(STR_DBPICK_OWNED));
      tiles += makerTiles(body, -1);
    }
    if (letters) sortCaption(body, T(STR_DBPICK_PICK_LETTER));
  }
  sortNewTile(body);
  return tiles;
}

static void showMakers() {
  s_ui.step = SS_MAKER;
  sortTitle(T(STR_DBPICK_MAKER_TITLE));
  bool used[SORT_LETTERS];
  const bool letters = lettersInUse(used) > 0;
  if (s_letter >= 0 && !used[s_letter]) s_letter = -1;
  lv_obj_t* body = newBody(letters);
  logLvMem("sortentry-makers/pre", 0);
  const int tiles = fillMakers(body, letters);
  logLvMem("sortentry-makers/post", tiles);
  if (letters) letterStrip(used);
}

// A letter tapped: its makers in place of the list, the letters stay where
// they were scrolled to. The marked letter again goes back to the overview.
static void showLetter(int letter) {
  if (s_ui.step != SS_MAKER || !s_strip || letter < 0 || letter >= SORT_LETTERS) return;
  s_letter = letter == s_letter ? -1 : letter;
  if (s_body) lv_obj_del(s_body);
  lv_obj_t* body = makeBody(true);
  logLvMem("sortentry-letter/pre", 0);
  const int tiles = fillMakers(body, true);
  logLvMem("sortentry-letter/post", tiles);
  markLetter();
}

void sortChooseMaker(const char* name) {
  if (!name || !name[0] || !sortData()) return;
  const SortChoices& brands = sortData()->brands;
  const int k = sortChoicesFind(brands, name);
  // name may point into the lists or into the entry itself: copied first.
  char chosen[sizeof(s_ui.entry.vendor)];
  utf8Cut(k >= 0 ? brands.item[k].name : name, sizeof(chosen) - 1, chosen, sizeof(chosen));
  memset(&s_ui.entry, 0, sizeof(s_ui.entry));
  s_ui.entry.diameter_mm = 1.75f;
  memcpy(s_ui.entry.vendor, chosen, sizeof(s_ui.entry.vendor));
  logSDf("SortEntry: maker %s%s", s_ui.entry.vendor, k >= 0 ? " (inventory)" : "");
  sortWaitShow();
  sortSourceBrand(s_ui.entry.vendor);
  linkWaitCardHide();
  sortShowMaterials();
}

// ------------------------------------------------------------
//  Opening, the index, closing
// ------------------------------------------------------------

void sortEntryClose() {
  sortKeysClose();
  // The card stands for the index load between the open and the first step,
  // and for the loads of a material chosen.
  if (s_index != IDX_NONE || s_ui.waiting) linkWaitCardHide();
  s_index = IDX_NONE;
  s_ui.waiting = false;
  s_ui.exists[0] = '\0';
  // A tap parked in the same pass must not open the entry again over the
  // screen that follows (deleteSpoolFlowOverlays() closes it from the loop).
  s_nav = SN_NONE;
  s_body = nullptr;
  s_strip = nullptr;
  s_title = nullptr;
  memset(s_letter_btn, 0, sizeof(s_letter_btn));
  releaseScreen(&s_scr);
  s_ui.step = SS_CLOSED;
  // Only an entry that was open has lists: the picker's entries stay alone.
  if (sortData()) sortSourceEnd();
}

// The maker step, or the material step of the maker the picker showed.
static void showFirstStep() {
  if (s_open_maker[0]) sortChooseMaker(s_open_maker);
  else showMakers();
}

static void startIndex() {
  // An index load already out (the picker's, whose wait was cancelled, or the
  // entry's own from an open before) is waited for, not refused: restarted
  // once it is in, fdbStartIndex() would free the index it brought.
  if (fdbState() != FDB_IDLE && fdbJob() == FDB_JOB_INDEX) {
    logSD("SortEntry: an index load is running, waiting for it");
    s_index = IDX_RUNNING;
    return;
  }
  if (fdbStartIndex()) { s_index = IDX_RUNNING; return; }
  // Busy with another load: again as soon as it is done. Refused while idle:
  // not before SORT_INDEX_RETRY_MS.
  s_index = IDX_WANT;
  s_index_ms = fdbState() == FDB_IDLE ? millis() : 0;
}

// The index is in, failed, or was given up: the first step with what there is.
static void indexDone() {
  s_index = IDX_NONE;
  showFirstStep();
  linkWaitCardHide();
}

static void openEntry(bool at_picker_maker) {
  logSD("SHOW: SortEntry");
  if (at_picker_maker) memcpy(s_open_maker, s_pick_maker, sizeof(s_open_maker));
  else s_open_maker[0] = '\0';
  closeCopyEntryPopup();
  dbPickClose();
  sortEntryClose();
  if (!sortSourceBegin()) {
    // Logged by the source. Back to the copy popup, from either way in.
    showCopyEntryPopup();
    return;
  }
  initStyles();
  buildFrame();
  s_letter = -1;
  s_index_again = false;
  memset(&s_ui.entry, 0, sizeof(s_ui.entry));
  s_ui.entry.diameter_mm = 1.75f;
  sortTitle(T(STR_DBPICK_MAKER_TITLE));
  // The index loads on its own task while the loop asks the inventory.
  if (fdbOffered() && !fdbIndexReady()) startIndex();
  sortWaitShow();
  sortSourceBrands();
  if (s_index == IDX_NONE) {
    showFirstStep();
    linkWaitCardHide();
  }
}

// The index load of the open, while the card stands. dbPickTick() takes any
// finished load, the picker closed or not, so this runs before it; fdbTake()
// puts the index in place whoever takes it.
static void indexTick() {
  if (s_index == IDX_WANT) {
    // Put in place by whoever took a load that ran meanwhile: nothing to start.
    if (fdbIndexReady()) {
      logSDf("SortEntry: index in place meanwhile, %d makers", makerCount());
      indexDone();
      return;
    }
    const bool idle = fdbState() == FDB_IDLE;
    if (idle ? (s_index_ms == 0 || millis() - s_index_ms >= SORT_INDEX_RETRY_MS) : fdbJob() == FDB_JOB_INDEX) startIndex();
  } else if (s_index == IDX_RUNNING) {
    linkWaitCardBytes(fdbBytes());
    const FdbState state = fdbState();
    if (state == FDB_DONE && fdbJob() == FDB_JOB_INDEX) {
      const int code = fdbResultCode();
      const bool current = fdbResultCurrent();
      fdbTake();
      logSDf("SortEntry: index HTTP %d, current %d, %d makers", code, (int)current, makerCount());
      indexDone();
      return;
    }
    if (state == FDB_IDLE) {
      // Taken by the picker's tick in the same pass. In place if it worked;
      // otherwise asked for once more, then the entry goes on without it.
      if (fdbIndexReady() || s_index_again) {
        logSDf("SortEntry: index taken by another tick, %d makers", makerCount());
        indexDone();
        return;
      }
      s_index_again = true;
      logSD("SortEntry: index taken by another tick, started again");
      startIndex();
    }
  }
  if (s_index != IDX_NONE && linkWaitCardCancelTake()) {
    // The load runs out on its own; the entry goes on with the inventory.
    logSD("SortEntry: index load cancelled");
    indexDone();
  }
}

// ------------------------------------------------------------
//  The loop's part
// ------------------------------------------------------------

static void goBack() {
  switch (s_ui.step) {
    case SS_WEIGHT:   sortShowColours(); break;
    case SS_RASTER:   sortShowColours(); break;
    case SS_COLOUR:   sortShowLines(); break;
    case SS_LINE:     sortShowMaterials(); break;
    case SS_MATERIAL: showMakers(); break;
    default:
      sortEntryClose();
      // Back to the copy popup the entry was opened from, as the picker does.
      showCopyEntryPopup();
      break;
  }
}

// "Not listed - add new": the keyboard of the step on screen.
static void newEntry() {
  if (s_ui.step == SS_MAKER)
    sortKeysShow(SK_TEXT, STR_SORT_KEY_MAKER, sizeof(s_ui.entry.vendor) - 1, sortMakerTyped);
  else if (s_ui.step == SS_MATERIAL)
    sortKeysShow(SK_TEXT, STR_SORT_KEY_MATERIAL, sizeof(s_ui.entry.material) - 1, sortMaterialTyped);
  else if (s_ui.step == SS_LINE)
    sortKeysShow(SK_TEXT, STR_SORT_KEY_LINE, sizeof(s_ui.entry.line) - 1, sortLineTyped);
  else if (s_ui.step == SS_COLOUR)
    sortKeysShow(SK_TEXT, STR_SORT_KEY_COLOUR, sizeof(s_ui.entry.colour) - 1, sortColourTyped);
}

static void runNav(uint8_t nav, int arg) {
  if (nav != SN_OPEN && !s_scr) return;
  switch (nav) {
    case SN_OPEN:   openEntry(arg != 0); break;
    case SN_CLOSE:  sortEntryClose(); break;
    case SN_BACK:   goBack(); break;
    case SN_NEW:    newEntry(); break;
    case SN_LETTER: showLetter(arg); break;
    case SN_MAKER: {
      const FdbMaker* m = fdbMaker(arg);
      if (m) sortChooseMaker(m->name);
      break;
    }
    case SN_BRAND:
      if (arg < brandCount()) sortChooseMaker(sortData()->brands.item[arg].name);
      break;
    case SN_LAST:   sortChooseMaker(s_ui.last_vendor); break;
    default:
      if (!sortStepsNav(nav, arg)) sortPickNav(nav, arg);
      break;
  }
}

void sortEntryTick() {
  indexTick();
  if (s_nav != SN_NONE) {
    const uint8_t nav = s_nav;
    const int arg = s_nav_arg;
    s_nav = SN_NONE;
    runNav(nav, arg);
  }
  sortKeysTick();
  // Last, right before dbPickTick(): a database load of the entry that has
  // finished is collected here before the picker's tick could take it
  // (sortSourceTick() starts one again that vanished, but the window stays
  // as small as one pass allows). The material step only reads its answer.
  sortMaterialLoads(sortSourceTick());
}
