#include "spool_type_list.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hardware/sd_logger.h"
#include "services/server_reach.h"
#include "services/spool_type_rules.h"
#include "services/spool_types.h"
#include "spool_type_name.h"
#include "theme.h"
#include "ui_common.h"
#include "lang.h"   // T(), last: its macro T(id) is a name other headers may use

// What a row or a button parks for spoolTypeListTick(). The handlers do no
// more than this; the loop answers, after the list is closed.
enum StlAct : uint8_t {
  STL_ACT_NONE = 0,
  STL_ACT_PICK,          // a row; s_park_idx holds its place in the catalog list
  STL_ACT_MEASURED,      // the measured weight, without a type
  STL_ACT_OTHER,         // NEAR -> VENDOR
  STL_ACT_SHOW_ALL,      // VENDOR -> ALL
  STL_ACT_ONLY_VENDOR,   // ALL -> VENDOR
  STL_ACT_NEW,           // a new type, named at the scale
  STL_ACT_SKIP,          // the button with skip_text
  STL_ACT_BACK,          // the back arrow of the header
  STL_ACT_RESHOW,        // the name entry is gone: the list again, rows rebuilt
};

// Where the list stands with the catalog. Only a load of its own that
// answered 200 counts: spoolTypesLoad() keeps the old list on a failure, and
// rows built from it would not be what the server has.
enum StlLoad : uint8_t { STL_LOAD_PENDING = 0, STL_LOAD_OK, STL_LOAD_FAILED };

// Layout. The list body is the house one (buildOptionList()), from under the
// header down to the bar. The bar is one row of up to three buttons, or two
// rows of two for four, so that the longest caption ("take measured" with its
// weight) stays on one line.
#define STL_LIST_Y    50
#define STL_EDGE      8
#define STL_GAP       6
#define STL_BTN_H     48
#define STL_BTNS_MAX  4
#define STL_ROW_W     456   // the inside of the list body, as wide as makeListBtn()
// "The type nearest to preselect_g, at any distance": a tolerance no catalog
// weight comes near.
#define STL_MARK_TOL_G 1.0e6f

static lv_obj_t *s_scr  = nullptr;
static lv_obj_t *s_list = nullptr;   // the rows, scrolling
static lv_obj_t *s_bar  = nullptr;   // the buttons under it

static SpoolTypeListReq s_req        = {};
static SpoolTypePicked  s_on_pick    = nullptr;
static void           (*s_on_skip)() = nullptr;
static void           (*s_on_back)() = nullptr;

static uint8_t s_load        = STL_LOAD_PENDING;
static int     s_load_code   = 0;
static bool    s_load_parked = false;   // the tick loads, once, after the open
static uint8_t s_park        = STL_ACT_NONE;
static int     s_park_idx    = -1;
// A name entry opened from here is up, and the list is hidden under it.
static bool s_naming = false;
// The maker has types in the loaded list: ALL offers "only <maker>" then.
static bool s_vendor_has = false;

// The name handed to on_pick, copied before the list closes.
static char s_pick_name[SPOOL_TYPE_NAME_MAX] = "";
// The places of one build in the catalog list. Static: SPOOL_TYPES_MAX ints
// do not belong on the loop's stack.
static int s_idx[SPOOL_TYPES_MAX];

bool isSpoolTypeListOpen() {
  return s_scr != nullptr;
}

void closeSpoolTypeList() {
  if (s_naming) closeSpoolTypeName();   // the name entry opened from here
  s_naming = false;
  // Deleted on the next timer pass: the close button of the header gets here
  // from inside its own event, through closeTareEntry().
  releaseScreen(&s_scr);
  s_list        = nullptr;
  s_bar         = nullptr;
  s_on_pick     = nullptr;
  s_on_skip     = nullptr;
  s_on_back     = nullptr;
  s_load_parked = false;
  s_park        = STL_ACT_NONE;
  s_park_idx    = -1;
}

// The caption of the skip button: the id the caller gave, or Cancel for one
// that is no row of the table.
static int stlSkipText() {
  const int id = s_req.skip_text;
  return (id >= 0 && id < STR_COUNT) ? id : STR_CANCEL;
}

// A measured weight that can be taken, or named as a new type: one a spool
// can have, which is all the name entry accepts. Zero (none) is not.
static bool stlMeasured() {
  return spoolTypeTarePlausible(s_req.measured_g);
}

// ---------------------------------------------------------------------------
//  Handlers: they park, nothing else
// ---------------------------------------------------------------------------

static void stlRowCb(lv_event_t *e) {
  if (s_park != STL_ACT_NONE) return;   // the first tap of a pass wins
  s_park_idx = (int)(intptr_t)lv_event_get_user_data(e);
  s_park     = STL_ACT_PICK;
}

static void stlButtonCb(lv_event_t *e) {
  if (s_park != STL_ACT_NONE) return;
  s_park = (uint8_t)(intptr_t)lv_event_get_user_data(e);
}

static void stlBackCb(lv_event_t *e) {
  (void)e;
  logSD("BTN: SpoolTypeList back");
  if (s_park == STL_ACT_NONE) s_park = STL_ACT_BACK;
}

// ---------------------------------------------------------------------------
//  Rows and buttons
// ---------------------------------------------------------------------------

// A line in the list that is no type: loading, a failure, nothing near, a
// weight no spool has, a list cut short.
static void stlNoteRow(const char *text, uint32_t col) {
  lv_obj_t *l = lv_label_create(s_list);
  lv_label_set_text(l, text);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, STL_ROW_W);
  lv_obj_set_style_pad_ver(l, 10, 0);
  lv_obj_set_style_text_font(l, UI_FONT_SMALL, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
}

// One type: its name on one line and its weight under it. The marked one
// carries the tick and the accent border, the others no arrow (a picker, as
// the time zones).
static lv_obj_t *stlTypeRow(const SpoolType &t, int idx, bool marked) {
  char grams[16];
  snprintf(grams, sizeof(grams), "%d g", t.grams);
  lv_obj_t *btn = makeListBtn(s_list, "", t.name, grams, marked);
  // The title is the second child, after the icon. A name too long for it
  // ends in dots; the dots need a height to end at, or the name wraps over
  // the weight.
  lv_obj_t *name = lv_obj_get_child(btn, 1);
  if (name) {
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_height(name, lv_font_get_line_height(lv_obj_get_style_text_font(name, 0)));
  }
  markListBtnChosen(btn, marked);
  lv_obj_add_event_cb(btn, stlRowCb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
  return btn;
}

// The rows of the mode, from the catalog list of the last load. Returns the
// marked row, if one was built. The old rows go at once (lv_obj_clean), so
// this runs from the loop; at the open there are none yet.
static lv_obj_t *stlBuildRows() {
  lv_obj_clean(s_list);
  char line[160];

  const bool is_near = s_req.mode == STL_NEAR;
  const bool near_ok = is_near && spoolTypeTarePlausible(s_req.near_g);
  if (is_near && !near_ok) {
    snprintf(line, sizeof(line), T(STR_SPT_IMPLAUSIBLE), s_req.near_g);
    stlNoteRow(line, UI_COL_WARN);
  }
  if (s_load == STL_LOAD_PENDING) {
    stlNoteRow(T(STR_SPT_LOADING), UI_COL_CAPTION);
    return nullptr;
  }
  if (s_load == STL_LOAD_FAILED) {
    snprintf(line, sizeof(line), T(STR_SPT_LOAD_FAIL), s_load_code);
    stlNoteRow(line, UI_COL_BAD);
    return nullptr;
  }
  if (is_near && !near_ok) return nullptr;   // the note says why there are no types

  const SpoolType *types = spoolTypesList();
  int n = types ? spoolTypesCount() : 0;
  if (n < 0) n = 0;
  if (n > SPOOL_TYPES_MAX) n = SPOOL_TYPES_MAX;
  int count = 0;
  int mark  = -1;
  if (is_near) {
    count = spoolTypeNear(types, n, s_req.vendor, s_req.near_g, SPOOL_TYPE_NEAR_G,
                          s_idx, SPOOL_TYPE_NEAR_MAX);
    if (count > 0) mark = s_idx[0];   // the closest
    else           stlNoteRow(T(STR_SPT_NONE_NEAR), UI_COL_CAPTION);
  } else {
    const int of_vendor = spoolTypeFilter(types, n, s_req.vendor, s_idx, SPOOL_TYPES_MAX);
    s_vendor_has = of_vendor > 0;
    if (s_req.mode == STL_VENDOR && !s_vendor_has) {
      // No type begins with the maker's name, or no maker was given: all of
      // them at once (design note L3).
      logSDf("UI: spool type list: no type of '%s', all shown", s_req.vendor);
      s_req.mode = STL_ALL;
    }
    if (s_req.mode == STL_VENDOR) {
      count = of_vendor;
    } else {
      for (int i = 0; i < n; i++) s_idx[i] = i;
      count = n;
    }
    int nearest = -1;
    if (spoolTypeNear(types, n, s_req.vendor, s_req.preselect_g, STL_MARK_TOL_G,
                      &nearest, 1) == 1) {
      mark = nearest;
    }
  }

  logLvMem("spooltypes/pre", 0);
  lv_obj_t *marked = nullptr;
  int shown = 0;
  for (int k = 0; k < count; k++) {
    const int i = s_idx[k];
    if (i < 0 || i >= n) continue;
    // A row is five objects. The list stops where internal RAM would run
    // short, and its last line says that it did.
    if (!lvPoolHasRoomForRow()) {
      logSDf("UI: spool type list cut at %d of %d rows, LVGL memory low", shown, count);
      stlNoteRow(T(STR_SPT_MORE), UI_COL_CAPTION);
      break;
    }
    lv_obj_t *row = stlTypeRow(types[i], i, i == mark);
    if (i == mark) marked = row;
    shown++;
  }
  logLvMem("spooltypes/post", shown);
  return marked;
}

static void stlButton(int x, int y, int w, uint8_t act, const char *text) {
  lv_obj_t *b = lv_btn_create(s_bar);
  lv_obj_set_size(b, w, STL_BTN_H);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_style_radius(b, UI_RADIUS_BTN, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  if (act == STL_ACT_SKIP) {
    // The way out, in the resting look of a secondary button.
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_POPUP_BORDER), LV_STATE_PRESSED);
    uiSecondaryFill(b);
  } else {
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_CHIP), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_PRESS_FILL), LV_STATE_PRESSED);
  }
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, w - 8);
  lv_obj_set_style_text_font(l, UI_FONT_SMALL, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, stlButtonCb, LV_EVENT_CLICKED, (void *)(intptr_t)act);
}

// The buttons of the mode, in the order of the design's table, and the list
// body cut to the room they leave. After stlBuildRows(), which may have
// turned VENDOR into ALL and found out whether the maker has types. ALL has
// two buttons at most: "only <maker>" or "add new", and skip.
static void stlBuildBar() {
  lv_obj_clean(s_bar);
  uint8_t acts[STL_BTNS_MAX];
  int n = 0;
  if (s_req.mode == STL_NEAR) {
    const bool offer = spoolTypeTarePlausible(s_req.near_g) && stlMeasured();
    if (offer) acts[n++] = STL_ACT_MEASURED;
    acts[n++] = STL_ACT_OTHER;
    if (offer) acts[n++] = STL_ACT_NEW;
  } else if (s_req.mode == STL_VENDOR) {
    acts[n++] = STL_ACT_SHOW_ALL;
    if (stlMeasured()) acts[n++] = STL_ACT_NEW;
  } else if (s_vendor_has) {
    // ALL by "show all": VENDOR, with "add new", is one tap away.
    acts[n++] = STL_ACT_ONLY_VENDOR;
  } else if (stlMeasured()) {
    // ALL because the maker has no type (VENDOR fell to ALL, L3): no way back
    // to VENDOR, and the type may be the one missing from the list.
    acts[n++] = STL_ACT_NEW;
  }
  acts[n++] = STL_ACT_SKIP;

  const int cols  = n == 4 ? 2 : n;
  const int rows  = (n + cols - 1) / cols;
  const int bar_h = rows * STL_BTN_H + (rows - 1) * STL_GAP;
  const int bar_y = 320 - STL_EDGE - bar_h;
  lv_obj_set_pos(s_bar, 0, bar_y);
  lv_obj_set_size(s_bar, 480, bar_h);
  lv_obj_set_height(s_list, bar_y - STL_GAP - STL_LIST_Y);

  const int w = (480 - 2 * STL_EDGE - (cols - 1) * STL_GAP) / cols;
  for (int k = 0; k < n; k++) {
    char text[80];
    switch (acts[k]) {
      case STL_ACT_MEASURED:
        snprintf(text, sizeof(text), T(STR_SPT_TAKE_MEASURED), spoolTypeGrams(s_req.measured_g));
        break;
      case STL_ACT_OTHER:
        copyT(text, sizeof(text), STR_SPT_OTHER);
        break;
      case STL_ACT_SHOW_ALL:
        copyT(text, sizeof(text), STR_SPT_SHOW_ALL);
        break;
      case STL_ACT_ONLY_VENDOR:
        // The whole maker: it is at most 31 bytes, nothing is cut.
        snprintf(text, sizeof(text), T(STR_SPT_ONLY_VENDOR), s_req.vendor);
        break;
      case STL_ACT_NEW:
        copyT(text, sizeof(text), STR_SPT_NEW);
        break;
      default:
        copyT(text, sizeof(text), stlSkipText());
        break;
    }
    stlButton(STL_EDGE + (k % cols) * (w + STL_GAP), (k / cols) * (STL_BTN_H + STL_GAP), w,
              acts[k], text);
  }
}

static void stlBuild() {
  lv_obj_t *marked = stlBuildRows();
  stlBuildBar();
  // After the bar: it sets the height of the list the row is scrolled into.
  if (marked) lv_obj_scroll_to_view(marked, LV_ANIM_OFF);
}

// ---------------------------------------------------------------------------
//  Answers, from the loop
// ---------------------------------------------------------------------------

// Closes the list and hands the type on. The name is copied first: it points
// into the catalog list, which the next load rewrites.
static void stlPicked(float grams, const char *name) {
  utf8Cut(name ? name : "", sizeof(s_pick_name) - 1, s_pick_name, sizeof(s_pick_name));
  SpoolTypePicked cb = s_on_pick;
  closeSpoolTypeList();
  if (cb) cb(grams, s_pick_name);
}

static void stlLeave(bool back) {
  void (*cb)() = (back && s_on_back) ? s_on_back : s_on_skip;
  logSDf("UI: spool type list closed, %s", back ? "back" : "skipped");
  closeSpoolTypeList();
  if (cb) cb();
}

// From spoolTypeNameTick(), in the loop: the new type is in the catalog. It
// is answered like a picked one.
static void stlNameCreated(float grams, const char *name) {
  s_naming = false;
  if (!s_scr) return;
  logSDf("UI: spool type list: new type '%s' %.0f g taken", name ? name : "", grams);
  stlPicked(grams, name);
}

// The name entry was cancelled (from its keyboard's event), or it refused to
// open (at once, from stlOpenName()). Parked: the tick shows the list again
// and builds its rows from the catalog as the name entry may have reloaded it.
static void stlNameCancelled() {
  s_naming = false;
  if (s_scr) s_park = STL_ACT_RESHOW;
}

static void stlOpenName() {
  if (!stlMeasured()) return;   // there is no button for it then
  // The maker and a blank, as the design has it; nothing for no maker.
  char prefill[sizeof(s_req.vendor) + 1];
  if (s_req.vendor[0]) snprintf(prefill, sizeof(prefill), "%s ", s_req.vendor);
  else                 prefill[0] = '\0';
  logSDf("UI: spool type list: new type for %.1f g", s_req.measured_g);
  // Hidden, not closed: a cancel brings it back. Marked before the call,
  // because a refused open calls back at once.
  lv_obj_add_flag(s_scr, LV_OBJ_FLAG_HIDDEN);
  s_naming = true;
  showSpoolTypeName(prefill, s_req.measured_g, stlNameCreated, stlNameCancelled);
}

void showSpoolTypeList(const SpoolTypeListReq &req, SpoolTypePicked on_pick,
                       void (*on_skip)(), void (*on_back)()) {
  if (!spoolTypesActive()) {
    logSD("UI: spool type list not opened, not Bambuddy in Spoolman mode");
    if (on_skip) on_skip();
    return;
  }
  closeSpoolTypeList();
  s_req = req;
  s_req.vendor[sizeof(s_req.vendor) - 1] = '\0';
  if (s_req.mode != STL_NEAR && s_req.mode != STL_VENDOR) s_req.mode = STL_ALL;
  s_on_pick    = on_pick;
  s_on_skip    = on_skip;
  s_on_back    = on_back;
  s_load       = STL_LOAD_PENDING;
  s_load_code  = 0;
  s_vendor_has = false;
  logSDf("SHOW: SpoolTypeList (mode %d, vendor '%s', near %.1f g, preselect %.1f g, measured %.1f g)",
         (int)s_req.mode, s_req.vendor, s_req.near_g, s_req.preselect_g, s_req.measured_g);

  s_scr = buildOverlayScreen();
  // The close button goes through showMainScreen(), whose
  // closeConfirmPopups() reaches closeTareEntry() and so this, without a
  // callback.
  buildSubHeader(s_scr, T(STR_SPT_TITLE), stlBackCb);
  s_list = buildOptionList(s_scr);
  lv_obj_set_y(s_list, STL_LIST_Y);
  s_bar = lv_obj_create(s_scr);
  lv_obj_set_style_bg_opa(s_bar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_bar, 0, 0);
  lv_obj_set_style_radius(s_bar, 0, 0);
  lv_obj_set_style_pad_all(s_bar, 0, 0);
  lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);

  // The loading line and the buttons of the mode; the tick loads and builds
  // both again.
  stlBuild();
  s_load_parked = true;
  lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_HIDDEN);
}

void spoolTypeListTick() {
  if (!s_scr) return;
  if (s_naming) {
    if (isSpoolTypeNameOpen()) return;   // hidden under it; it answers by callback
    // Gone without an answer (closed from elsewhere): the list must not stay
    // open and hidden.
    s_naming = false;
    s_park   = STL_ACT_RESHOW;
  }
  if (s_load_parked) {
    s_load_parked = false;
    // The loading line is painted before the request blocks the loop.
    lv_refr_now(NULL);
    s_load_code = serverReachNote(spoolTypesLoad(), true);
    s_load = s_load_code == 200 ? STL_LOAD_OK : STL_LOAD_FAILED;
    logSDf("UI: spool type list loaded, HTTP %d, %d types", s_load_code,
           s_load == STL_LOAD_OK ? spoolTypesCount() : 0);
    stlBuild();
    return;
  }
  const uint8_t act = s_park;
  const int     idx = s_park_idx;
  s_park     = STL_ACT_NONE;
  s_park_idx = -1;
  switch (act) {
    case STL_ACT_PICK: {
      // The rows are built again after every load, so the place is the one
      // the row was built from; checked all the same.
      const SpoolType *types = spoolTypesList();
      if (s_load != STL_LOAD_OK || !types || idx < 0 || idx >= spoolTypesCount()) {
        logSDf("UI: spool type row %d ignored, the list has %d", idx, spoolTypesCount());
        return;
      }
      logSDf("UI: spool type picked: '%s' %d g", types[idx].name, types[idx].grams);
      stlPicked((float)types[idx].grams, types[idx].name);
      return;
    }
    case STL_ACT_MEASURED:
      if (!stlMeasured()) return;
      logSDf("UI: spool type list: measured %.1f g taken", s_req.measured_g);
      stlPicked(s_req.measured_g, "");
      return;
    case STL_ACT_OTHER:
    case STL_ACT_ONLY_VENDOR:
    case STL_ACT_SHOW_ALL:
      // The same load, the rows of another mode.
      s_req.mode = act == STL_ACT_SHOW_ALL ? STL_ALL : STL_VENDOR;
      logSDf("UI: spool type list mode %d", (int)s_req.mode);
      stlBuild();
      return;
    case STL_ACT_NEW:
      stlOpenName();
      return;
    case STL_ACT_RESHOW:
      lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_HIDDEN);
      stlBuild();
      return;
    case STL_ACT_SKIP:
      stlLeave(false);
      return;
    case STL_ACT_BACK:
      stlLeave(true);
      return;
    default:
      return;
  }
}
