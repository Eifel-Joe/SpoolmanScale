#include "tare_entry.h"

#include <Arduino.h>
#include <lvgl.h>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "app/app_state.h"
#include "app_config.h"
#include "confirm_popup.h"
#include "empty_spool_type.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/ams_assign.h"     // AMS_SETTLE_MS / AMS_SETTLE_TOL_G
#include "services/spool_extras.h"
#include "services/spool_types.h"
#include "services/user_options.h"   // g_scale_fitted
#include "spool_type_list.h"
#include "spool_type_name.h"
#include "theme.h"
#include "ui_common.h"

// Issue #40: a spool that is still in use has no empty reel to weigh, but its
// empty weight is often known - printed on the box, measured on a sister
// spool, or listed by the maker. The weight popup's Options offer typing it in
// next to weighing it, and a typed number then takes the same way to the
// server as a weighed one. With Bambuddy in Spoolman mode the Options have one
// empty weight row instead, which asks first how the number is to be had: it
// also offers a type from the catalog of empty spools and the full spool,
// whose empty weight is the reading less the filament on it.

static lv_obj_t *s_choice = nullptr;   // weigh, type, spool type or full spool
static lv_obj_t *s_pad    = nullptr;   // the keypad

static char      s_input[8]  = "";
static lv_obj_t *s_lbl_input = nullptr;
static lv_obj_t *s_lbl_info  = nullptr;
static lv_obj_t *s_btn_next  = nullptr;
// The keypad takes the filament of a full spool instead of an empty weight.
static bool      s_pad_fill  = false;
// The colour the line under the input was last drawn in.
static uint32_t  s_info_col  = 0;

// Longest input: "600.0", plus room for the one decimal digit being typed.
#define TARE_INPUT_MAX_CHARS  5
// The filament of a full spool: whole grams, 1 to 9999 (design L5).
#define FULL_INPUT_MAX_CHARS  4
#define FULL_FILL_MAX_G       9999
// The fill amounts offered as buttons beside the keys (design L5).
static const int FULL_FILL_PRESETS_G[4] = { 1000, 750, 500, 250 };

// Lifted, by the same measure the removal check in app_loop.cpp uses: more
// than half of what was on the pad when the entry opened has left it.
#define TARE_ENTRY_GONE_FRACTION 0.5f

// The spool the entry was opened for, and what the pad carried then. Typing
// takes long enough for the spool to be swapped meanwhile, and a number typed
// for one spool must not land on the next (issue #40 follow-up).
static int   s_spool_id = 0;
static float s_open_g   = 0.0f;

// The list of spool types, when this file opened it for the spool s_spool_id
// (the first weighing, or a spool type or a full spool from the choice).
// Cleared on every way the list closes.
static bool s_list_mine = false;

// The full spool on its way through the list: the filament typed in and the
// reading it was taken with, handed on with the type picked.
static float s_full_fill = 0.0f;
static float s_full_bare = 0.0f;

// A pick from a list of the choice parked its write in this pass. The list
// answers from tareEntryTick(), after the loop ran the parked jobs, so the
// write runs on the next pass; until then the entry still counts as open,
// or automatic weighing would weigh against the old empty weight in between.
static bool s_pick_parked = false;

// The windows of this file, opened for the spool s_spool_id. Only these are
// closed by a swap or a lift (tareEntryTick()).
static bool spoolEntryOpen() {
  return s_choice != nullptr || s_pad != nullptr ||
         (s_list_mine && isSpoolTypeListOpen());
}

bool isTareEntryOpen() {
  // The empty spool screen of the settings counts as well, although it
  // belongs to no spool: a spool is put on the pad for it, and automatic
  // weighing and the drift correction must not act on that.
  return spoolEntryOpen() || s_pick_parked || isEmptySpoolTypeOpen();
}

// The choice and the keypad, nothing else. What the buttons of this file
// close when their own window is done.
static void closeSpoolEntry() {
  releaseScreen(&s_choice);
  releaseScreen(&s_pad);
  s_lbl_input = nullptr;
  s_lbl_info  = nullptr;
  s_btn_next  = nullptr;
}

void closeTareEntry() {
  closeSpoolEntry();
  // For the navigation (closeConfirmPopups()): the screens of the empty spool
  // types go as well, the list of types and a name entry also when they were
  // not opened from here.
  closeEmptySpoolType();
  closeSpoolTypeList();
  closeSpoolTypeName();
  s_list_mine = false;
}

// ---------------------------------------------------------------------------
//  The list of spool types, from the choice (design L13)
// ---------------------------------------------------------------------------

// Cancel or back: nothing is written.
static void entryListLeft() {
  s_list_mine = false;
}

// "Spool type": the type goes to the spool as its empty weight, by the job
// the scope question's Spool button parks; a spool swapped meanwhile drops it
// there. No weight is written: the next weighing is taken against the type.
static void entryTypePicked(float grams, const char *name) {
  s_list_mine   = false;
  s_pick_parked = true;
  logSDf("UI: spool type for spool %d: '%s' %.1f g taken",
         s_spool_id, name ? name : "", grams);
  confirmParkTare(TARE_SCOPE_SPOOL, grams, s_spool_id, false);
}

// "Full spool": whatever was picked (a type, the computed weight, a type
// added from the list) is the tare, and label and rest follow from it
// (confirm_popup.cpp, design L14).
static void entryFullPicked(float grams, const char *name) {
  s_list_mine   = false;
  s_pick_parked = true;
  logSDf("UI: full spool, spool %d: '%s' %.1f g taken (%.0f g filament, %.1f g bare)",
         s_spool_id, name ? name : "", grams, s_full_fill, s_full_bare);
  confirmParkFullSpool(grams, s_full_fill, s_full_bare, s_spool_id);
}

// The list for the spool of the choice, one of this file's windows: it counts
// in isTareEntryOpen(), and a swap or a lift closes it (tareEntryTick()).
static void openEntryList(const SpoolTypeListReq &req, SpoolTypePicked on_pick) {
  s_list_mine = true;   // before the call: a refused open calls back at once
  showSpoolTypeList(req, on_pick, entryListLeft);
}

// ---------------------------------------------------------------------------
//  The keypad
// ---------------------------------------------------------------------------

// Something is on the pad that can only be a spool. Below that the reading is
// noise or nothing, and the typed value is not compared against it.
static bool padHoldsSpool() {
  return scale_weight_g > NEW_SPOOL_TARE_MIN_G;
}

// The typed value, when it may be stored. The range is the one a derived tare
// has to pass (confirm_popup.cpp, newSpoolDerivesTare), and a spool on the pad
// has to outweigh its own empty reel.
static bool inputValue(float *out) {
  if (!s_input[0]) return false;
  const float v = atof(s_input);
  if (v < NEW_SPOOL_TARE_MIN_G || v > NEW_SPOOL_TARE_MAX_G) return false;
  // Against the spool alone: the parts on it are not the spool's weight.
  if (padHoldsSpool() && v >= spoolExtrasBareReading()) return false;
  if (out) *out = v;
  return true;
}

// The typed filament of a full spool, when it can be one: whole grams, 1 to
// FULL_FILL_MAX_G. Whether the empty weight it gives is plausible is the
// list's question; it says so there.
static bool fillValue(int *out) {
  if (!s_input[0]) return false;
  const int v = atoi(s_input);
  if (v < 1 || v > FULL_FILL_MAX_G) return false;
  if (out) *out = v;
  return true;
}

// The full spool's line: the reading without the parts, the filament, and
// the empty weight they leave (design L5), in a warning colour where that is
// no spool or no filament has been typed yet.
static void fullInfo(char *info, size_t n, uint32_t *col) {
  int fill = 0;
  const bool ok = fillValue(&fill);
  const float bare = spoolExtrasBareReading();
  const float tare = spoolTypeTareFromFull(bare, (float)fill);
  snprintf(info, n, T(STR_FULL_INFO), bare, (float)fill, tare);
  *col = (ok && spoolTypeTarePlausible(tare)) ? UI_COL_GOOD : UI_COL_WARN;
}

// Input line, the line under it and the look of the Next button, all from
// the current input. Called after every key.
static void refreshPad() {
  if (!s_lbl_input || !s_lbl_info || !s_btn_next) return;

  char buf[24];
  snprintf(buf, sizeof(buf), "%s g", s_input[0] ? s_input : "_");
  lv_label_set_text(s_lbl_input, buf);

  float v = 0.0f;
  const bool ok = s_pad_fill ? fillValue(nullptr) : inputValue(&v);
  char info[96];
  uint32_t info_col = UI_COL_CAPTION;
  if (s_pad_fill) {
    fullInfo(info, sizeof(info), &info_col);
  } else if (ok && padHoldsSpool()) {
    // What a weighing would store once this tare is in - the number the
    // issue asked to see before anything is saved.
    const float bare = spoolExtrasBareReading();
    snprintf(info, sizeof(info), T(STR_TARE_ENTER_REST), bare, v, bare - v);
    info_col = UI_COL_GOOD;
  } else if (s_input[0] && padHoldsSpool() && atof(s_input) >= spoolExtrasBareReading()) {
    copyT(info, sizeof(info), STR_TARE_ENTER_OVER);
    info_col = UI_COL_WARN;
  } else {
    snprintf(info, sizeof(info), T(STR_TARE_ENTER_RANGE), NEW_SPOOL_TARE_MIN_G, NEW_SPOOL_TARE_MAX_G);
    if (s_input[0] && !ok) info_col = UI_COL_WARN;
  }
  lv_label_set_text(s_lbl_info, info);
  lv_obj_set_style_text_color(s_lbl_info, lv_color_hex(info_col), 0);
  s_info_col = info_col;

  lv_obj_set_style_bg_color(s_btn_next, lv_color_hex(ok ? UI_COL_OK_BG : UI_COL_SURFACE), 0);
  lv_obj_set_style_text_color(lv_obj_get_child(s_btn_next, 0),
                              lv_color_hex(ok ? UI_COL_OK_TEXT : UI_COL_CAPTION), 0);
}

// The full spool's line follows the reading while its keypad is up (design
// L5: live), from tareEntryTick(). Drawn again only when its text or colour
// would change, so a still reading costs a format and a compare per pass.
static void fullPadTick() {
  if (!s_pad || !s_pad_fill || !s_lbl_info) return;
  char info[96];
  uint32_t col = UI_COL_WARN;
  fullInfo(info, sizeof(info), &col);
  if (col == s_info_col && strcmp(info, lv_label_get_text(s_lbl_info)) == 0) return;
  lv_label_set_text(s_lbl_info, info);
  lv_obj_set_style_text_color(s_lbl_info, lv_color_hex(col), 0);
  s_info_col = col;
}

static void onKey(lv_event_t *e) {
  const char *ch = lv_label_get_text(lv_obj_get_child(lv_event_get_target(e), 0));
  const size_t len = strlen(s_input);
  if (s_pad_fill) {
    // Whole grams, without a zero in front.
    if (ch[0] == '.' || (len == 0 && ch[0] == '0')) return;
    if (len >= FULL_INPUT_MAX_CHARS) return;
  } else {
    const char *dot = strchr(s_input, '.');
    if (ch[0] == '.') {
      if (dot || len == 0) return;           // one point, and not in front
    } else if (dot && strlen(dot) > 1) {
      return;                                // one decimal is plenty for grams
    }
    if (len >= TARE_INPUT_MAX_CHARS) return;
  }
  s_input[len] = ch[0];
  s_input[len + 1] = '\0';
  refreshPad();
}

// A fill amount button: its grams replace whatever was typed.
static void onFillPreset(lv_event_t *e) {
  const int g = (int)(intptr_t)lv_event_get_user_data(e);
  snprintf(s_input, sizeof(s_input), "%d", g);
  refreshPad();
}

// Next on the keypad of an empty weight: on to where it is stored.
static void onTareNext(lv_event_t *e) {
  float v = 0.0f;
  if (!inputValue(&v)) return;          // the button is greyed, the press is ignored
  // tareEntryTick() closes the pad on a swap; this covers a swap that
  // landed in the same pass as the press.
  if (sm_id != s_spool_id) {
    logSDf("UI: tare %.1f g typed for spool %d, spool %d loaded now - dropped",
           v, s_spool_id, sm_id);
    closeSpoolEntry();
    return;
  }
  logSDf("UI: tare typed in, %.1f g", v);
  closeSpoolEntry();
  showSpoolWeightScope(v, false);
}

// Next on the full spool's keypad: the computed empty weight and the types
// near it (design L5). The reading is taken at the press, without the parts
// on the spool; it goes on with the pick, so the rest is the one the list
// was computed from.
static void onFullNext(lv_event_t *e) {
  int fill = 0;
  if (!fillValue(&fill)) return;        // the button is greyed, the press is ignored
  // As for a typed tare: a swap in the same pass as the press.
  if (sm_id != s_spool_id) {
    logSDf("UI: full spool, %d g typed for spool %d, spool %d loaded now - dropped",
           fill, s_spool_id, sm_id);
    closeSpoolEntry();
    return;
  }
  const float bare      = spoolExtrasBareReading();
  const float tare      = spoolTypeTareFromFull(bare, (float)fill);
  const bool  plausible = spoolTypeTarePlausible(tare);
  logSDf("UI: full spool, spool %d: %d g filament on %.1f g, empty spool %.1f g%s",
         s_spool_id, fill, bare, tare, plausible ? "" : " (implausible)");
  closeSpoolEntry();
  s_full_fill = (float)fill;
  s_full_bare = bare;
  SpoolTypeListReq req = {};
  req.mode = STL_NEAR;
  utf8Cut(sm_vendor_g, sizeof(req.vendor) - 1, req.vendor, sizeof(req.vendor));
  req.near_g = tare;
  // Only an empty weight a spool can have is offered to take or to name.
  req.measured_g = plausible ? tare : 0.0f;
  req.skip_text  = STR_CANCEL;
  openEntryList(req, entryFullPicked);
}

// fill: the full spool's filament (design L5) instead of an empty weight:
// whole grams, the label weight filled in where the spool has one, and the
// fill amount buttons above Next.
static void showTarePad(bool fill) {
  logSD(fill ? "SHOW: TareEntryPad (full spool)" : "SHOW: TareEntryPad");
  releaseScreen(&s_pad);
  s_pad_fill = fill;
  s_input[0] = '\0';
  if (fill && sm_total > 0.0f) {
    const long label = lroundf(sm_total);
    if (label >= 1 && label <= FULL_FILL_MAX_G) snprintf(s_input, sizeof(s_input), "%ld", label);
  }

  lv_obj_t *scr = buildOverlayScreen();
  s_pad = scr;
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, T(fill ? STR_FULL_TITLE : STR_TARE_ENTER_TITLE));
  lv_obj_set_style_text_color(title, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_text_font(title, UI_FONT_SMALL, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

  // Keys on the left, the value above them; what the value means and the
  // two ways out on the right.
  const int KEY_W = 90, KEY_H = 48, GAP = 6, EDGE = 10;
  const int PAD_W = 3 * KEY_W + 2 * GAP;
  const int KEYS_Y = 100;
  const int COL_X = EDGE + PAD_W + EDGE;
  const int COL_W = 480 - COL_X - EDGE;

  lv_obj_t *box = lv_obj_create(scr);
  lv_obj_set_size(box, PAD_W, 52);
  lv_obj_set_pos(box, EDGE, 38);
  lv_obj_set_style_bg_color(box, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_border_color(box, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_radius(box, UI_RADIUS_INPUT, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  s_lbl_input = lv_label_create(box);
  lv_obj_set_style_text_color(s_lbl_input, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_text_font(s_lbl_input, UI_FONT_ICON, 0);
  lv_obj_center(s_lbl_input);

  s_lbl_info = lv_label_create(scr);
  lv_label_set_long_mode(s_lbl_info, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_lbl_info, COL_W);
  lv_obj_set_pos(s_lbl_info, COL_X, 38);
  lv_obj_set_style_text_font(s_lbl_info, UI_FONT_CAPTION, 0);

  static const char *const KEYS[12] = { "1","2","3","4","5","6","7","8","9",".","0","" };
  for (int i = 0; i < 12; i++) {
    if (fill && KEYS[i][0] == '.') continue;   // whole grams: its place stays empty
    const int x = EDGE + (i % 3) * (KEY_W + GAP);
    const int y = KEYS_Y + (i / 3) * (KEY_H + GAP);
    lv_obj_t *b = padButton(scr, x, y, KEY_W, KEY_H, UI_COL_SURFACE, UI_COL_LINE);
    if (KEYS[i][0]) {
      padLabel(b, KEYS[i], UI_COL_INK, UI_FONT_HEADLINE);
      lv_obj_add_event_cb(b, onKey, LV_EVENT_CLICKED, NULL);
    } else {
      padLabel(b, LV_SYMBOL_BACKSPACE, UI_COL_INK_2, UI_FONT_BODY);
      lv_obj_add_event_cb(b, [](lv_event_t *e) {
        const size_t len = strlen(s_input);
        if (len) s_input[len - 1] = '\0';
        refreshPad();
      }, LV_EVENT_CLICKED, NULL);
    }
  }

  // Next starts level with the second row of keys, so the hint above it has
  // room to breathe; Cancel keeps the bottom.
  const int KEYS_H = 4 * KEY_H + 3 * GAP;
  const int CANCEL_H = UI_TOUCH_MIN + 10;
  const int CANCEL_Y = KEYS_Y + KEYS_H - CANCEL_H;
  // On the full spool's pad the fill amounts take that room instead, two by
  // two above Next, each a finger's height; Next starts below them.
  const int PRESET_H = UI_TOUCH_MIN;
  const int NEXT_Y = fill ? KEYS_Y + 2 * (PRESET_H + GAP) : KEYS_Y + KEY_H + GAP;
  const int NEXT_H = CANCEL_Y - GAP - NEXT_Y;

  if (fill) {
    const int PRESET_W = (COL_W - GAP) / 2;
    for (int i = 0; i < 4; i++) {
      const int x = COL_X + (i % 2) * (PRESET_W + GAP);
      const int y = KEYS_Y + (i / 2) * (PRESET_H + GAP);
      lv_obj_t *b = padButton(scr, x, y, PRESET_W, PRESET_H, UI_COL_SURFACE, UI_COL_LINE);
      char pbuf[12];
      snprintf(pbuf, sizeof(pbuf), "%d g", FULL_FILL_PRESETS_G[i]);
      padLabel(b, pbuf, UI_COL_INK, UI_FONT_BODY);
      lv_obj_add_event_cb(b, onFillPreset, LV_EVENT_CLICKED,
                          (void *)(intptr_t)FULL_FILL_PRESETS_G[i]);
    }
  }

  char next_buf[32];
  snprintf(next_buf, sizeof(next_buf), "%s  " LV_SYMBOL_RIGHT, T(STR_BTN_NEXT));
  s_btn_next = padButton(scr, COL_X, NEXT_Y, COL_W, NEXT_H, UI_COL_OK_BG, UI_COL_OK_BG_PRESSED);
  padLabel(s_btn_next, next_buf, UI_COL_OK_TEXT, UI_FONT_BODY);
  lv_obj_add_event_cb(s_btn_next, fill ? onFullNext : onTareNext, LV_EVENT_CLICKED, NULL);

  lv_obj_t *cancel = padButton(scr, COL_X, CANCEL_Y, COL_W, CANCEL_H,
                               UI_COL_BAD_BG, UI_COL_BAD_BG_PRESSED);
  padLabel(cancel, T(STR_CANCEL), UI_COL_BAD_TEXT, UI_FONT_SMALL);
  styleLeaveButton(cancel);
  lv_obj_add_event_cb(cancel, [](lv_event_t *e) { closeSpoolEntry(); }, LV_EVENT_CLICKED, NULL);

  refreshPad();
}

// Opened from the weight popup's Options, which offer weighing the empty
// spool beside it, and from the choice below.
void showTareEntryPad() {
  closeSpoolEntry();
  s_spool_id = sm_id;
  s_open_g   = scale_weight_g;
  showTarePad(false);
}

// ---------------------------------------------------------------------------
//  Weigh, type, spool type or full spool
// ---------------------------------------------------------------------------

static lv_obj_t *choiceButton(lv_obj_t *box, int y, int w, int h, const char *text) {
  lv_obj_t *b = lv_btn_create(box);
  lv_obj_set_size(b, w, h);
  lv_obj_set_pos(b, 12, y);
  lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_CHIP), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_radius(b, UI_RADIUS_BTN, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  padLabel(b, text, UI_COL_INK_2, UI_FONT_SMALL);
  return b;
}

void showTareChoice() {
  logSD("SHOW: TareChoice");
  closeSpoolEntry();
  s_spool_id = sm_id;
  s_open_g   = scale_weight_g;

  lv_obj_t *scrim = lv_obj_create(lv_scr_act());
  s_choice = scrim;
  lv_obj_set_size(scrim, 480, 320);
  lv_obj_set_pos(scrim, 0, 0);
  lv_obj_set_style_bg_color(scrim, lv_color_hex(UI_COL_SCRIM), 0);
  lv_obj_set_style_bg_opa(scrim, UI_OPA_SCRIM, 0);
  lv_obj_set_style_border_width(scrim, 0, 0);
  lv_obj_set_style_radius(scrim, 0, 0);
  lv_obj_set_style_pad_all(scrim, 0, 0);
  lv_obj_clear_flag(scrim, LV_OBJ_FLAG_SCROLLABLE);

  // Bambuddy in Spoolman mode (design L13, L16): two by two, weigh and type
  // in, then spool type and full spool. Weighing and the full spool need the
  // spool on the pad, and a button not offered leaves its place empty. The
  // box is as wide as the weight popup this opens from, and its places are
  // counted inside the border, where LVGL puts the children. Every other
  // backend keeps its column: weigh and type in, one above the other, weighing
  // offered with the pad empty too (the press takes the reading then, an empty
  // reel may go down meanwhile). Both have two rows, so the cancel row and the
  // box height share a sum.
  //
  // choiceButton() is upstream's (the column's x of 12); lv_obj_set_x() then
  // places each button, so the grid needs no sixth parameter (house rule).
  const bool grid   = spoolTypesActive();
  const bool on_pad = padHoldsSpool();
  const int BORDER = 2, BTN_H = 70, TOP = 44, BOTTOM = 10;
  const int BOX_W  = grid ? 460 : 440;
  const int EDGE   = grid ? 6 : 12;
  const int GAP    = grid ? 6 : 10;
  const int BTN_W  = grid ? (BOX_W - 2 * BORDER - 2 * EDGE - GAP) / 2 : BOX_W - 2 * EDGE;
  const int X_R    = EDGE + BTN_W + GAP;         // the grid's right column
  const int ROW2_Y = TOP + BTN_H + GAP;
  const int Y_CANCEL = TOP + 2 * (BTN_H + GAP);
  const int BOX_H  = 2 * BORDER + Y_CANCEL + UI_TOUCH_MIN + BOTTOM;
  lv_obj_t *box = lv_obj_create(scrim);
  lv_obj_set_size(box, BOX_W, BOX_H);
  lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(box, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_border_color(box, lv_color_hex(UI_COL_POPUP_BORDER), 0);
  lv_obj_set_style_border_width(box, BORDER, 0);
  lv_obj_set_style_radius(box, UI_RADIUS_BOX, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(box);
  lv_label_set_text(title, T(STR_TARE_CHOICE_TITLE));
  lv_obj_set_style_text_color(title, lv_color_hex(UI_COL_INK), 0);
  lv_obj_set_style_text_font(title, UI_FONT_TITLE, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

  if (!grid || on_pad) {
    char weigh_buf[96];
    // The empty spool without the parts on it: they are not the spool's.
    snprintf(weigh_buf, sizeof(weigh_buf), T(STR_TARE_CHOICE_WEIGH), spoolExtrasBareReading());
    lv_obj_t *weigh = choiceButton(box, TOP, BTN_W, BTN_H, weigh_buf);
    lv_obj_set_x(weigh, EDGE);
    lv_obj_add_event_cb(weigh, [](lv_event_t *e) {
      // The reading at the moment of the press, as the old button took it.
      const float g = spoolExtrasBareReading();
      closeSpoolEntry();
      showSpoolWeightScope(g, true);
    }, LV_EVENT_CLICKED, NULL);
  }

  char type_buf[96];
  snprintf(type_buf, sizeof(type_buf), LV_SYMBOL_KEYBOARD "  %s", T(STR_TARE_CHOICE_TYPE));
  lv_obj_t *type = choiceButton(box, grid ? TOP : ROW2_Y, BTN_W, BTN_H, type_buf);
  lv_obj_set_x(type, grid ? X_R : EDGE);
  lv_obj_add_event_cb(type, [](lv_event_t *e) { showTareEntryPad(); }, LV_EVENT_CLICKED, NULL);

  if (grid) {
    // The maker's types, the one nearest the spool's empty weight marked.
    lv_obj_t *by_type = choiceButton(box, ROW2_Y, BTN_W, BTN_H, T(STR_TARE_CHOICE_TYPES));
    lv_obj_set_x(by_type, EDGE);
    lv_obj_add_event_cb(by_type, [](lv_event_t *e) {
      SpoolTypeListReq req = {};
      req.mode = STL_VENDOR;
      utf8Cut(sm_vendor_g, sizeof(req.vendor) - 1, req.vendor, sizeof(req.vendor));
      req.preselect_g = sm_spool_weight;
      req.skip_text   = STR_CANCEL;
      closeSpoolEntry();
      openEntryList(req, entryTypePicked);
    }, LV_EVENT_CLICKED, NULL);
  }

  if (grid && on_pad) {
    lv_obj_t *full = choiceButton(box, ROW2_Y, BTN_W, BTN_H, T(STR_TARE_CHOICE_FULL));
    lv_obj_set_x(full, X_R);
    lv_obj_add_event_cb(full, [](lv_event_t *e) {
      releaseScreen(&s_choice);
      showTarePad(true);
    }, LV_EVENT_CLICKED, NULL);
  }

  lv_obj_t *cancel = padButton(box, EDGE, Y_CANCEL, grid ? 2 * BTN_W + GAP : BTN_W,
                               UI_TOUCH_MIN, UI_COL_BAD_BG, UI_COL_BAD_BG_PRESSED);
  lv_obj_set_style_radius(cancel, UI_RADIUS_BTN, 0);
  padLabel(cancel, T(STR_CANCEL), UI_COL_BAD_TEXT, UI_FONT_SMALL);
  // The original's way out: red outline and a cross in front, the fill and
  // the caption colour are the style's own (the keypad's Cancel above).
  styleLeaveButton(cancel);
  lv_obj_add_event_cb(cancel, [](lv_event_t *e) { closeSpoolEntry(); }, LV_EVENT_CLICKED, NULL);
}

// ---------------------------------------------------------------------------
//  The first weighing
// ---------------------------------------------------------------------------

// Bambuddy in Spoolman mode: a spool put down for the first time in this
// session is asked which empty spool it sits on, once (design 3, L18). What
// is asked is spoolTypeFirstAsk()'s answer; the list is opened for the spool
// like the choice and the keypad, so a swap or a lift closes it.

// The spools looked at in this session, asked or not. A ring, the oldest
// place goes first; 0 is a free place, spool ids start at 1.
#define FIRST_WEIGH_ASKED_MAX 16
static int     s_fw_asked[FIRST_WEIGH_ASKED_MAX] = {};
static uint8_t s_fw_asked_next = 0;

// The reading has to stand still first: within AMS_SETTLE_TOL_G for
// AMS_SETTLE_MS, the AMS question's measure (app_loop.cpp), far shorter than
// the 3 s of automatic weighing, which then waits for the answer. A watch of
// its own, started again for every spool.
static int           s_fw_id    = 0;
static float         s_fw_last  = 0.0f;
static unsigned long s_fw_since = 0;

// The spool whose question opened on this placement, until its tag is gone or
// another spool is loaded: automatic weighing leaves it alone, whatever the
// answer (tareEntryHoldsAutoWeigh()). 0: none.
static int s_fw_hold_id = 0;

// What the first weighing needs before anything else: Bambuddy in Spoolman
// mode, a load cell, and a spool with a tag that the server knows, not
// archived.
static bool fwSpoolHere() {
  return spoolTypesActive() && wifi_ok && g_scale_fitted && scale_ready &&
         tag_present && sm_found && sm_id > 0 && !sm_archived;
}

static bool fwAsked(int id) {
  for (int i = 0; i < FIRST_WEIGH_ASKED_MAX; i++)
    if (s_fw_asked[i] == id) return true;
  return false;
}

static void fwRemember(int id) {
  s_fw_asked[s_fw_asked_next] = id;
  s_fw_asked_next = (uint8_t)((s_fw_asked_next + 1) % FIRST_WEIGH_ASKED_MAX);
}

void tareEntryForgetAsked() {
  memset(s_fw_asked, 0, sizeof(s_fw_asked));
  s_fw_asked_next = 0;
  s_fw_id = 0;
  s_fw_hold_id = 0;
}

// Root: since U a spool created at the scale without a type of its own, under
// a filament or brand with an empty weight, keeps that weight inherited (L21),
// so it has no tare of its own and firstWeighTick() asked for its type on the
// same placement, right after our list had asked it during the create.
// Fix: the create marks the new id the way firstWeighTick() marks a spool it
// looked at, and only when the create's own input carries the answer
// (TagCreateInput::spool_type_asked, copy_flow.cpp copy_create_asked).
// NOT-TO-DO: no flag "skip the next new spool" set when the list answers: a
// create cancelled or failed after the answer would leave it set for the
// next, different spool.
void tareEntryFirstWeighDone(int spool_id) {
  if (spool_id <= 0) return;
  if (!fwAsked(spool_id)) fwRemember(spool_id);
  logSDf("UI: first weighing of spool %d: answered at creation", spool_id);
}

bool tareEntryHoldsAutoWeigh() {
  // Asked on this placement: automatic weighing writes no rest in between (a
  // type picked writes its own, fwPicked(); "take measured" leaves the rest as
  // the server has it), and rest and tare stay untouched after "later", so
  // that the question comes again after a restart.
  if (s_fw_hold_id != 0 && tag_present && sm_id == s_fw_hold_id) return true;
  // Not looked at yet: it may still be asked, also once a dialog that holds
  // the question back is gone.
  return fwSpoolHere() && !fwAsked(sm_id);
}

// From the list, in the loop, after it closed itself. A spool swapped
// meanwhile drops the parked job there (confirm_popup.cpp).
//
// V19 (spec 5.3 M), a type from the catalog (also one just added): the rest
// is written too, by the job of the full spool (CJ_FULL_SPOOL): tare, label
// weight (only where it changes), rest, each only after the one before it.
// The fill is the label weight sm_total, the reading the one without the
// parts on the spool.
// Root: the tare alone left the rest as the server had it, and a spool
// holding more than its label (spool 2, 25.10.2026: 1184 g on a 167 g type,
// 1017 g of rest, label 1000 g) cannot be given its rest by a plain weight
// write: Spoolman and Bambuddy cut a rest above the label silently. So the
// label is lifted first where the rest is above it; the job does it in whole
// grams (spoolTypeFullSpoolWrite, test_spool_type_rules.py).
// "Take measured" (no name) writes the tare alone, as before.
// NOT-TO-DO: no write sequence of its own here (a second copy of the order
// and the rounding rules), and not a tare job followed by a weight job (one
// job slot, and the rest would be worked out against the unrounded tare).
static void fwPicked(float grams, const char *name) {
  s_list_mine = false;
  const bool with_rest = name && name[0];
  logSDf("UI: first weighing of spool %d: '%s' %.1f g taken, %s",
         s_spool_id, name ? name : "", grams,
         with_rest ? "rest written too" : "empty weight only");
  if (with_rest)
    confirmParkFullSpool(grams, sm_total, spoolExtrasBareReading(), s_spool_id);
  else
    confirmParkTare(TARE_SCOPE_SPOOL, grams, s_spool_id, false);
}

// "Later", or back: the spool stays asked until the next restart.
static void fwLater() {
  s_list_mine = false;
  logSDf("UI: first weighing of spool %d: later", s_spool_id);
}

static void firstWeighTick() {
  if (!fwSpoolHere()) {
    s_fw_id = 0;
    return;
  }
  // The watch runs on under a dialog: it does not make the reading less still.
  const unsigned long now = millis();
  if (sm_id != s_fw_id || fabsf(scale_weight_g - s_fw_last) > AMS_SETTLE_TOL_G) {
    s_fw_id    = sm_id;
    s_fw_last  = scale_weight_g;
    s_fw_since = now;
    return;
  }
  if (now - s_fw_since < AMS_SETTLE_MS) return;
  if (isTareEntryOpen() || uiModalWaiting() || isSpoolTypeListOpen() ||
      isSpoolTypeNameOpen())
    return;
  if (fwAsked(sm_id)) return;
  // A tag held to the reader over an empty pad is no weighing; the spool is
  // looked at once it is put down.
  const float bare = spoolExtrasBareReading();
  if (!(bare > NEW_SPOOL_TARE_MIN_G)) return;

  // The label weight is spoolTypeFullLabel(), not sm_total: an unused spool
  // filled over its label would get an empty weight too high by the overfill.
  // No label weight stays none, and such a spool is asked from the list.
  const float label    = sm_total > 0.0f ? spoolTypeFullLabel(sm_total, sm_remaining) : 0.0f;
  const float computed = bare - label;
  const SpoolTypeAsk ask = spoolTypeFirstAsk(label, sm_remaining, bare,
                                             sm_tare_source == TARE_SPOOL, sm_spool_weight);
  // Before anything opens, and also when there is nothing to ask.
  fwRemember(sm_id);
  static const char *const ASK_WORDS[] = { "nothing to ask", "suggest", "implausible", "list" };
  logSDf("UI: first weighing of spool %d: %s (computed %.0f g, stored %.0f g)",
         sm_id, ask <= STA_LIST ? ASK_WORDS[ask] : "?", computed, sm_spool_weight);
  if (ask == STA_NONE) return;

  SpoolTypeListReq req = {};
  utf8Cut(sm_vendor_g, sizeof(req.vendor) - 1, req.vendor, sizeof(req.vendor));
  req.skip_text = STR_SPT_LATER;
  if (ask == STA_LIST) {
    req.mode = STL_VENDOR;           // in use: nothing measured, nothing marked
  } else {
    req.mode   = STL_NEAR;
    req.near_g = computed;
    // Only an empty weight a spool can have is offered to take or to name.
    if (ask == STA_SUGGEST) req.measured_g = computed;
  }
  closeTareEntry();
  s_spool_id   = sm_id;
  s_open_g     = scale_weight_g;
  s_list_mine  = true;   // before the call: a refused open calls fwLater() at once
  // From the open on, not only after an answer: a pick, "later", back, the
  // close button and a lift that leaves the tag on the reader all end the
  // question without a weighing on this placement.
  s_fw_hold_id = sm_id;
  showSpoolTypeList(req, fwPicked, fwLater);
}

void tareEntryTick() {
  // A pick parked in the pass before has been written by now: the loop runs
  // the parked jobs before this tick.
  s_pick_parked = false;
  // The list goes without a call back when another flow opens its own
  // (showSpoolTypeList() closes the one standing), and a flag left set would
  // claim the next list, that flow's, as this file's.
  if (s_list_mine && !isSpoolTypeListOpen()) s_list_mine = false;
  // The placement the first weighing held ends with the tag or a swap; a
  // spool put back is weighed as usual.
  if (s_fw_hold_id != 0 && (!tag_present || sm_id != s_fw_hold_id)) s_fw_hold_id = 0;
  // Every loop pass, before anything returns: the list of types, the name
  // entry and the empty spool screen have no call of their own in
  // app_loop.cpp, which must not grow.
  spoolTypeListTick();
  spoolTypeNameTick();
  emptySpoolTypeTick();
  firstWeighTick();
  fullPadTick();
  // What follows is for the windows opened for a spool. The empty spool
  // screen weighs whatever is put down, and a swap does not close it.
  if (!spoolEntryOpen()) return;
  const bool swapped = sm_id != s_spool_id;
  // Only where a spool was on the pad to begin with: the entry also serves a
  // spool that sits in the printer, and an empty pad has nothing to lose.
  const bool lifted = s_open_g > NEW_SPOOL_TARE_MIN_G &&
                      (s_open_g - scale_weight_g) > s_open_g * TARE_ENTRY_GONE_FRACTION;
  if (!swapped && !lifted) return;
  logSDf("UI: tare entry for spool %d closed unsaved, %s (%.0f g -> %.0f g)",
         s_spool_id, swapped ? "another spool loaded" : "spool lifted",
         s_open_g, scale_weight_g);
  // From the loop, not from a callback; releaseScreen() deletes on the next
  // timer pass either way.
  closeSpoolEntry();
  if (s_list_mine) closeSpoolTypeList();   // no answer: the spool stays asked
  s_list_mine = false;
}
