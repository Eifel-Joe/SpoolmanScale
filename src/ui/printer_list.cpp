#include "printer_list.h"

#include <lvgl.h>
#include <stdint.h>

#include "hardware/lvgl_mem.h"
#include "hardware/sd_logger.h"
#include "services/ams_slots.h"
#include "services/klipper_rules.h"
#include "services/printer_pick_rules.h"
#include "colour_search.h"
#include "theme.h"
#include "ui_common.h"
#include "lang.h"   // T(), last: its macro T(id) is a name other headers may use

// What a button or the back arrow parks for printerListTick(). The handlers
// do no more than this; the loop answers, after the window is closed.
enum PrlAct : uint8_t {
  PRL_ACT_NONE = 0,
  PRL_ACT_PICK,     // a printer's button; s_park_idx holds its place in the list
  PRL_ACT_CANCEL,   // Cancel under the list: leaves at once, a word or not
  PRL_ACT_BACK,     // the back arrow of the header: ends a search first
};

// Layout, in the points of the overlay (480 x 320 on every board,
// buildOverlayScreen()). The list body is the house one (buildOptionList()),
// 456 wide inside, from under the header down to 6 above the bar; the bar is
// Cancel alone, across the screen less the edge.
#define PRL_LIST_Y      50
#define PRL_EDGE        8
#define PRL_BTN_H       48
#define PRL_BAR_Y       (320 - PRL_EDGE - PRL_BTN_H)
#define PRL_LIST_GAP    6     // between the list and the bar
#define PRL_ROW_W       456   // the inside of the list body: a line across it
// One button per printer, two to a row: (456 - 8) / 2. High enough for the
// name (font 16, line 18) over the kind (font 12, line 16) with room around.
#define PRL_CELL_W      224
#define PRL_CELL_H      56
#define PRL_CELL_GAP_X  8
#define PRL_CELL_GAP_Y  6
#define PRL_CELL_PAD    10    // inside a button, on every side
#define PRL_CELL_RADIUS 10    // as makeListBtn()
#define PRL_NAME_W      186   // 224 - 2 * 10 - 18 for the tick
#define PRL_ART_W       204   // 224 - 2 * 10: the kind has no tick beside it

static lv_obj_t *s_scr   = nullptr;
static lv_obj_t *s_list  = nullptr;   // the buttons, scrolling
static lv_obj_t *s_title = nullptr;   // the header's title, narrowed by the search head

// The search in the window (V47): the magnifier of the colour lists, with the
// texts of this window. It is there for every list, however short: the rule
// finds a printer by its kind as well ("klipper", "a1"), which a scroll
// through the names does not.
static ColourSearch s_find = { "", nullptr, nullptr, false, false, STR_PRL_FIND_KEY, STR_PRL_FIND_NONE };

// The caller's list and the printer on its screen, while the window is open.
// The list is the caller's (the AMS view's, in PSRAM): never read after a close.
static const AmsPrinterList *s_printers   = nullptr;
static int                   s_current_id = 0;
static void                (*s_on_pick)(int printer_id) = nullptr;
static void                (*s_on_leave)()              = nullptr;

static uint8_t s_park     = PRL_ACT_NONE;
static int     s_park_idx = -1;

bool isPrinterListOpen() {
  return s_scr != nullptr;
}

void closePrinterList() {
  // Deleted on the next timer pass: the close button of the header gets here
  // from inside its own event.
  releaseScreen(&s_scr);
  s_list  = nullptr;
  s_title = nullptr;
  // The head goes with the screen released above, the word with the window.
  colourSearchClose(&s_find);
  s_printers = nullptr;
  s_on_pick  = nullptr;
  s_on_leave = nullptr;
  s_park     = PRL_ACT_NONE;
  s_park_idx = -1;
}

// The screen the search's keyboard is put over; none once the window is
// closed, which takes the keyboard with it.
static lv_obj_t *prlScreen() {
  return s_scr;
}

// A printer of the list as the rule reads it. The rule has names of its own
// for the layouts, so it needs neither ams_slots.h nor klipper_rules.h.
// A Klipper printer that was never tested (KL_LAYOUT_NONE) is one with a
// single tool, as KL_LAYOUT_SINGLE: "Klipper", without a number.
static PrinterPickRow prlRowOf(const AmsPrinter &p) {
  PrinterPickRow r = {};
  r.name  = p.name;
  r.kind  = p.kind == AMS_PRINTER_KLIPPER ? PPK_KLIPPER : PPK_BAMBU;
  r.model = p.model;
  r.bays  = p.bays;
  switch (p.layout) {
    case KL_LAYOUT_TOOLS: r.layout = PPL_TOOLCHANGER; break;
    case KL_LAYOUT_MMU:   r.layout = PPL_MMU;         break;
    case KL_LAYOUT_AFC:   r.layout = PPL_AFC;         break;
    default:              r.layout = PPL_SINGLE;      break;
  }
  return r;
}

// ---------------------------------------------------------------------------
//  Handlers: they park, nothing else
// ---------------------------------------------------------------------------

static void prlPickCb(lv_event_t *e) {
  if (s_park != PRL_ACT_NONE) return;   // the first tap of a pass wins
  s_park_idx = (int)(intptr_t)lv_event_get_user_data(e);
  s_park     = PRL_ACT_PICK;
}

static void prlCancelCb(lv_event_t *e) {
  (void)e;
  logSD("BTN: PrinterList cancel");
  if (s_park == PRL_ACT_NONE) s_park = PRL_ACT_CANCEL;
}

static void prlBackCb(lv_event_t *e) {
  (void)e;
  logSD("BTN: PrinterList back");
  if (s_park == PRL_ACT_NONE) s_park = PRL_ACT_BACK;
}

// ---------------------------------------------------------------------------
//  The buttons
// ---------------------------------------------------------------------------

// A line across the list that is no printer: the list cut short.
static void prlNoteRow(const char *text) {
  lv_obj_t *l = lv_label_create(s_list);
  lv_label_set_text(l, text);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, PRL_ROW_W);
  lv_obj_set_style_pad_ver(l, 10, 0);
  lv_obj_set_style_text_font(l, UI_FONT_SMALL, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_CAPTION), 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
}

// One printer: its name on one line, its kind under it, the colours and the
// press of a list row (makeListBtn()). The current one carries the tick and
// the accent border. idx is the printer's place in the caller's list.
static lv_obj_t *prlPrinterBtn(const AmsPrinter &p, const char *art, int idx, bool current) {
  lv_obj_t *btn = lv_btn_create(s_list);
  lv_obj_set_size(btn, PRL_CELL_W, PRL_CELL_H);
  lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COL_ROW), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COL_ROW_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn, PRL_CELL_RADIUS, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_border_width(btn, current ? 2 : 1, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(current ? UI_COL_ACCENT : UI_COL_ROW_PRESSED), 0);
  lv_obj_set_style_pad_all(btn, 0, 0);

  // The dots of a long name need a height to end at, or the name wraps over
  // the kind.
  const lv_coord_t name_h = lv_font_get_line_height(UI_FONT_BODY);
  lv_obj_t *name = lv_label_create(btn);
  lv_obj_set_style_text_font(name, UI_FONT_BODY, 0);
  lv_obj_set_style_text_color(name, lv_color_hex(UI_COL_INK), 0);
  lv_label_set_text(name, p.name);
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  lv_obj_set_size(name, PRL_NAME_W, name_h);
  if (art[0]) {
    lv_obj_set_pos(name, PRL_CELL_PAD, PRL_CELL_PAD);
    lv_obj_t *kind = lv_label_create(btn);
    lv_obj_set_style_text_font(kind, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(kind, lv_color_hex(UI_COL_CAPTION), 0);
    lv_label_set_text(kind, art);
    lv_label_set_long_mode(kind, LV_LABEL_LONG_DOT);
    lv_obj_set_size(kind, PRL_ART_W, lv_font_get_line_height(UI_FONT_CAPTION));
    lv_obj_set_pos(kind, PRL_CELL_PAD, PRL_CELL_PAD + name_h);
  } else {
    // No kind (FilaMan, the AMS source, a Bambu printer without a model):
    // the name alone, in the middle of the button.
    lv_obj_align(name, LV_ALIGN_LEFT_MID, PRL_CELL_PAD, 0);
  }

  if (current) {
    lv_obj_t *tick = lv_label_create(btn);
    lv_obj_set_style_text_font(tick, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(tick, lv_color_hex(UI_COL_ACCENT), 0);
    lv_label_set_text(tick, LV_SYMBOL_OK);
    lv_obj_align(tick, LV_ALIGN_TOP_RIGHT, -PRL_CELL_PAD, PRL_CELL_PAD);
  }
  lv_obj_add_event_cb(btn, prlPickCb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
  return btn;
}

// The buttons of the printers the word leaves, from the caller's list.
// Returns the current printer's button, if one was built. The old buttons go
// at once (lv_obj_clean), so this runs from the loop; at the open there are
// none yet.
static lv_obj_t *prlBuildButtons() {
  lv_obj_clean(s_list);
  const PrinterArtTexts texts = { T(STR_PRL_KLIPPER), T(STR_PRL_TOOLS), T(STR_PRL_MMU), T(STR_PRL_AFC) };
  const int n = s_printers->count > AMS_MAX_PRINTERS ? AMS_MAX_PRINTERS : s_printers->count;

  logLvMem("printers/pre", 0);
  lv_obj_t *marked = nullptr;
  int shown = 0;
  // Root: under a word the buttons are fewer than the printers, so the n-th
  //   button is not the n-th printer.
  // Fix: a button carries i, its printer's place in the caller's list, and
  //   printerListTick() reads the id there; the list stays as it is while
  //   the window is open.
  // NOT-TO-DO: carrying the count of the buttons shown: it is the place of a
  //   printer only as long as no word is set.
  for (int i = 0; i < n; i++) {
    const AmsPrinter &p = s_printers->p[i];
    const PrinterPickRow row = prlRowOf(p);
    char art[PRINTER_ART_MAX];
    printerPickArt(row, texts, art, sizeof(art));
    // The one filter: the name or the kind holds the word. The tick mark is
    // set on a button that is built, never on one the search left out.
    if (!printerPickShows(row, art, s_find.word)) continue;
    // A button is three objects, four with the tick. The list stops where
    // internal RAM would run short, and its last line says that it did.
    // After the filter: a printer the word leaves out must not cut the list.
    // Before the button: the reserve is asked for the button about to be made.
    if (!lvPoolHasRoomForRow()) {
      logSDf("UI: printer list cut at %d of %d printers, LVGL memory low", shown, n);
      prlNoteRow(T(STR_PRL_MORE));
      break;
    }
    const bool is_current = p.id == s_current_id;
    lv_obj_t *btn = prlPrinterBtn(p, art, i, is_current);
    if (is_current) marked = btn;
    shown++;
  }
  logLvMem("printers/post", shown);
  if (shown == 0) colourSearchNone(&s_find, s_list, PRL_ROW_W);   // a line under a word only
  // Always, not only with the verbose log: what one printer costs is read off
  // these lines at the acceptance test.
  const LvMemStats m = lvMemStats();
  logSDf("UI: printer list, %d of %d printers, word '%s', lvgl int=%u ps=%u", shown, n, s_find.word,
         (unsigned)m.int_used, (unsigned)m.ps_used);
  return marked;
}

// Cancel across the bottom, in the style of the original (red outline and
// cross), applied once the caption is on it. Built once, at the open: the
// search builds the list again, never the bar.
static void prlBuildCancel() {
  const int w = 480 - 2 * PRL_EDGE;
  lv_obj_t *b = lv_btn_create(s_scr);
  lv_obj_set_size(b, w, PRL_BTN_H);
  lv_obj_set_pos(b, PRL_EDGE, PRL_BAR_Y);
  lv_obj_set_style_radius(b, UI_RADIUS_BTN, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, T(STR_CANCEL));
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, w - 8);
  lv_obj_set_style_text_font(l, UI_FONT_SMALL, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(l);
  styleLeaveButton(b);
  lv_obj_add_event_cb(b, prlCancelCb, LV_EVENT_CLICKED, nullptr);
}

static void prlBuild() {
  if (!s_scr || !s_printers) return;
  // The search head first, while the old buttons still stand: it is three
  // objects, and a list cut at the row reserve must not leave it without a
  // place (colour_search.cpp). Always with prlBuild: the magnifier is there
  // for every list.
  colourSearchTitle(&s_find, s_scr, s_title, T(STR_PRL_TITLE), prlBuild);
  lv_obj_t *marked = prlBuildButtons();
  // The height of the list is set at the open, so the button can be scrolled
  // into it; scroll_to_view lays the flex rows out first.
  if (marked) lv_obj_scroll_to_view(marked, LV_ANIM_OFF);
}

// ---------------------------------------------------------------------------
//  Answers, from the loop
// ---------------------------------------------------------------------------

// The printer at idx of the caller's list. The id is read before the close:
// the list is not read after it.
static void prlPicked(int idx) {
  const int count = s_printers ? s_printers->count : 0;
  if (idx < 0 || idx >= count || idx >= AMS_MAX_PRINTERS) {
    logSDf("UI: printer list, button %d ignored, the list has %d", idx, count);
    return;
  }
  const int id = s_printers->p[idx].id;
  logSDf("UI: printer list, picked %d '%s'", id, s_printers->p[idx].name);
  void (*cb)(int) = s_on_pick;
  closePrinterList();
  if (cb) cb(id);
}

static void prlLeave(const char *how) {
  void (*cb)() = s_on_leave;
  logSDf("UI: printer list closed, %s", how);
  closePrinterList();
  if (cb) cb();
}

// The title label of the header, for the search head to narrow: buildSubHeader()
// of the original keeps no pointer to it (and stays as it is). It is the child
// after the back button; a header that is built otherwise leaves the title as
// it is, and the search head sits beside it.
static void prlTakeTitle() {
  lv_obj_t *t = lv_obj_get_child(s_scr, 1);
  s_title = (t && lv_obj_check_type(t, &lv_label_class)) ? t : nullptr;
  if (!s_title) return;
  // Narrowed by the head, the title stays centred on one line: the dots need
  // a height to end at, or the text wraps.
  lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);
  lv_obj_set_height(s_title, lv_font_get_line_height(lv_obj_get_style_text_font(s_title, 0)));
  lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
}

void showPrinterList(const AmsPrinterList *list, int current_id, void (*on_pick)(int printer_id),
                     void (*on_leave)()) {
  closePrinterList();
  if (!list) {
    logSD("UI: printer list not opened, no list");
    if (on_leave) on_leave();
    return;
  }
  s_printers   = list;
  s_current_id = current_id;
  s_on_pick    = on_pick;
  s_on_leave   = on_leave;
  logSDf("SHOW: PrinterList (%d printers, current %d)", (int)list->count, current_id);

  s_scr = buildOverlayScreen();
  buildSubHeader(s_scr, T(STR_PRL_TITLE), prlBackCb);
  prlTakeTitle();
  s_list = buildOptionList(s_scr);
  lv_obj_set_y(s_list, PRL_LIST_Y);
  lv_obj_set_height(s_list, PRL_BAR_Y - PRL_LIST_GAP - PRL_LIST_Y);
  // Two columns: the buttons flow in rows that wrap, 224 + 8 + 224 = 456,
  // the inside of the list body.
  lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_column(s_list, PRL_CELL_GAP_X, 0);
  lv_obj_set_style_pad_row(s_list, PRL_CELL_GAP_Y, 0);
  prlBuildCancel();

  prlBuild();
  lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_HIDDEN);
}

void printerListTick() {
  if (!s_scr) return;
  // The head's tap, the word from the keyboard: the buttons are built again.
  // A tap parked in the same pass keeps its meaning, because it carries the
  // printer's place in the list, not a button.
  colourSearchTick(&s_find, prlScreen);
  const uint8_t act = s_park;
  const int     idx = s_park_idx;
  s_park     = PRL_ACT_NONE;
  s_park_idx = -1;
  switch (act) {
    case PRL_ACT_PICK:
      prlPicked(idx);
      return;
    case PRL_ACT_CANCEL:
      prlLeave("cancelled");
      return;
    case PRL_ACT_BACK:
      // A search ends first, and the window stays.
      if (colourSearchBack(&s_find)) return;
      prlLeave("back");
      return;
    default:
      return;
  }
}
