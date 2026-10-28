#include "ui/colour_search.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "hardware/sd_logger.h"
#include "services/filament_db.h"
#include "services/text_util.h"
#include "ui/sort_entry_internal.h"
#include "ui/theme.h"
#include "ui/ui_common.h"
// Last: lang.h defines T(), which a template parameter of a header above
// would trip over.
#include "lang.h"

// The frame of the link lists as db_pick_screen.cpp (DBP_HEAD_*) and
// sort_entry_screen.cpp (SORT_HEAD_*) build it: buttons of 44 at 4 from the
// edges of a header 52 high, the title centred between back and close.
#define CS_HEAD_BTN   44
#define CS_GAP        4
#define CS_TITLE_W    (LV_HOR_RES - 2 * (CS_HEAD_BTN + 2 * CS_GAP))
// The search keeps this far from the close button: a finger meant for the x
// of the word must not close the list. Its right edge from the screen's.
#define CS_SEP        8
#define CS_RIGHT      (CS_GAP + CS_HEAD_BTN + CS_SEP)
// The word with its x: 110 px for the word, "transparent" is 97 in the body
// font; a longer one ends in dots.
#define CS_CHIP_W     140
#define CS_CHIP_PAD   6
#define CS_CHIP_X_W   18
// The magnifier is drawn: the fonts have no glyph for one, nor has LVGL 8 a
// symbol. A ring, and a handle down to the right that starts at its rim,
// centred in the 44 px button.
#define CS_RING       20
#define CS_RING_X     9
#define CS_RING_Y     8
#define CS_RING_LINE  3
#define CS_HANDLE_X   25
#define CS_HANDLE_Y   24
#define CS_HANDLE_W   4

// lv_line keeps a pointer to its points: they live as long as the firmware.
static const lv_point_t CS_HANDLE[2] = { { 0, 0 }, { 8, 8 } };

// The one the keyboard was opened for: the word that comes back goes there.
static ColourSearch* s_asking = nullptr;

// A tap on the head only parks; colourSearchTick() carries it out on the loop.
static void onHeadTap(lv_event_t* e) {
  ColourSearch* s = (ColourSearch*)lv_event_get_user_data(e);
  if (s) s->tapped = true;
}

// A button of the header, the look of the frame's back button: the surface,
// no frame, the press filled.
static lv_obj_t* headButton(lv_obj_t* scr, ColourSearch* s, int w) {
  lv_obj_t* b = lv_btn_create(scr);
  lv_obj_set_size(b, w, CS_HEAD_BTN);
  lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -CS_RIGHT, CS_GAP);
  lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_radius(b, UI_RADIUS_BTN, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_add_event_cb(b, onHeadTap, LV_EVENT_CLICKED, s);
  return b;
}

static lv_obj_t* magnifier(lv_obj_t* scr, ColourSearch* s) {
  lv_obj_t* b = headButton(scr, s, CS_HEAD_BTN);
  lv_obj_t* ring = lv_obj_create(b);
  lv_obj_set_size(ring, CS_RING, CS_RING);
  lv_obj_set_pos(ring, CS_RING_X, CS_RING_Y);
  lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ring, CS_RING_LINE, 0);
  lv_obj_set_style_border_color(ring, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_pad_all(ring, 0, 0);
  // The tap is the button's, not the ring's.
  lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* handle = lv_line_create(b);
  lv_line_set_points(handle, CS_HANDLE, 2);
  lv_obj_set_pos(handle, CS_HANDLE_X, CS_HANDLE_Y);
  lv_obj_set_style_line_width(handle, CS_HANDLE_W, 0);
  lv_obj_set_style_line_rounded(handle, true, 0);
  lv_obj_set_style_line_color(handle, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_clear_flag(handle, LV_OBJ_FLAG_CLICKABLE);
  return b;
}

// The word with its x, framed in the accent so it reads as a filter that is
// on. The whole of it is the x: one tap ends the search.
static lv_obj_t* chip(lv_obj_t* scr, ColourSearch* s) {
  lv_obj_t* b = headButton(scr, s, CS_CHIP_W);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_t* word = lv_label_create(b);
  lv_label_set_text(word, s->word);
  lv_label_set_long_mode(word, LV_LABEL_LONG_DOT);
  // One line: the dots need a height to end at, or the text wraps.
  lv_obj_set_size(word, CS_CHIP_W - 2 * CS_CHIP_PAD - CS_CHIP_X_W, lv_font_get_line_height(UI_FONT_BODY));
  lv_obj_set_style_text_font(word, UI_FONT_BODY, 0);
  lv_obj_set_style_text_color(word, lv_color_hex(UI_COL_INK), 0);
  lv_obj_align(word, LV_ALIGN_LEFT_MID, CS_CHIP_PAD, 0);
  lv_obj_t* x = lv_label_create(b);
  lv_label_set_text(x, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_font(x, UI_FONT_TITLE, 0);
  lv_obj_set_style_text_color(x, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_align(x, LV_ALIGN_RIGHT_MID, -CS_CHIP_PAD, 0);
  return b;
}

void colourSearchTitle(ColourSearch* s, lv_obj_t* scr, lv_obj_t* title, const char* text, void (*redraw)()) {
  if (!s) return;
  // Built anew with every step, from the loop: never from the head's own tap,
  // which only parks.
  if (s->head) lv_obj_del(s->head);
  s->head = nullptr;
  s->tapped = false;
  s->redraw = redraw;
  int room = 0;
  if (redraw && scr) {
    if (lvPoolHasRoomForRow()) {
      s->head = s->word[0] ? chip(scr, s) : magnifier(scr, s);
      room = (s->word[0] ? CS_CHIP_W : CS_HEAD_BTN) + CS_SEP;
    } else {
      logSD("ColourSearch: LVGL pool low, no search in the header");
    }
  }
  if (!title) return;
  lv_label_set_text(title, text ? text : "");
  // Centred in what the head leaves between back and close, 4 px before it.
  lv_obj_set_width(title, CS_TITLE_W - room);
  lv_obj_set_x(title, -room / 2);
}

// The keyboard's OK, on the loop (sortKeysTick()): the word is kept, and the
// screen's tick builds the step with it in the same pass (the entry's tick
// runs after the keys', the picker's after the entry's).
static void wordTyped(const char* text) {
  if (!s_asking || !text) return;
  utf8Cut(text, sizeof(s_asking->word) - 1, s_asking->word, sizeof(s_asking->word));
  s_asking->typed = true;
}

void colourSearchTick(ColourSearch* s, lv_obj_t* (*under)()) {
  if (!s || !(s->tapped || s->typed)) return;
  const bool typed = s->typed;
  s->tapped = false;
  s->typed = false;
  // For a screen that is gone, hidden (hideSpoolFlowOverlays()), or on a step
  // without a search: nothing to build.
  lv_obj_t* scr = under ? under() : nullptr;
  if (!scr || lv_obj_has_flag(scr, LV_OBJ_FLAG_HIDDEN) || !s->redraw) return;
  if (typed) {
    logSDf("ColourSearch: \"%s\"", s->word);
    s->redraw();
  } else if (s->word[0]) {
    logSDf("BTN: colour search \"%s\" ended", s->word);
    s->word[0] = '\0';
    s->redraw();
  } else {
    logSD("BTN: colour search");
    s_asking = s;
    sortKeysShowOver(under, SK_TEXT, STR_COLOUR_FIND_KEY, COLOUR_SEARCH_MAX - 1, wordTyped);
  }
}

bool colourSearchBack(ColourSearch* s) {
  if (!s || !s->word[0]) return false;
  logSDf("BTN: back, colour search \"%s\" ended", s->word);
  s->word[0] = '\0';
  if (s->redraw) s->redraw();
  return true;
}

void colourSearchClose(ColourSearch* s) {
  if (!s) return;
  // The head is a child of the screen that goes: not deleted here.
  s->word[0] = '\0';
  s->head = nullptr;
  s->redraw = nullptr;
  s->tapped = false;
  s->typed = false;
  if (s_asking == s) s_asking = nullptr;
}

bool colourSearchShows(const ColourSearch* s, const char* name) {
  return !s || colourSearchMatch(name, s->word);
}

bool colourSearchFdb(const ColourSearch* s, const FdbEntry& e, const char* maker, const char* material) {
  if (!s || !s->word[0]) return true;
  char shown[FDB_NAME_MAX];
  fdbEntryDisplayName(e, maker, material, shown, sizeof(shown));
  return colourSearchMatch(shown, s->word);
}

void colourSearchNone(const ColourSearch* s, lv_obj_t* body, int width) {
  if (!s || !s->word[0] || !body || !lvPoolHasRoomForRow()) return;
  char text[COLOUR_SEARCH_MAX + 64];
  snprintf(text, sizeof(text), T(STR_COLOUR_FIND_NONE), s->word);
  // A caption across the list, as the screens draw theirs.
  lv_obj_t* l = lv_label_create(body);
  lv_label_set_text(l, text);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, width);
  lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_CAPTION), 0);
  lv_obj_set_style_text_font(l, UI_FONT_SMALL, 0);
  lv_obj_set_style_pad_top(l, CS_GAP, 0);
}
