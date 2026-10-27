#include "tare_scope_popup.h"
#include "confirm_popup.h"
#include "ui_common.h"
#include "app/app_state.h"
#include "services/backend.h"
#include "services/backend_api.h"

#include <Arduino.h>
#include <lvgl.h>
#include <cstdio>

#include "lang.h"
#include "theme.h"

// ============================================================
//  WHERE AN EMPTY SPOOL WEIGHT GOES
//
//  The question asked once a tare is known, whether it was weighed, derived
//  by the New spool button or typed in: this spool, its filament, or the
//  brand. Out of confirm_popup.cpp, which was close to its line budget. A
//  button here does not write anything: it hands the value to
//  confirmParkTare(), and the write runs from appLoop().
//
//  Its look is the one the original gave the question on 26.10.2026 with the
//  new weight popup: the three places in words rather than backend field
//  names, each with what it reaches, and the one whose value applies now
//  saying so. The original built it back into confirm_popup.cpp; it stays
//  here, with the answer parked through confirmParkTare() as before.
// ============================================================

static lv_obj_t *s_scope_popup = nullptr;   // where the empty spool weight goes

// The weight the scope buttons below will store. Normally whatever is on the
// pad, but a brand new spool can derive it instead -- see the New spool button.
static float s_tare_prompt_g = 0.0f;
// The spool an empty spool weight was meant for. Every tare write goes to
// whatever spool is loaded when it runs, and a spool swapped while the scope
// question stood would take a tare that belongs to the one before it.
static int   s_tare_spool_id = 0;   // the spool the scope question is for
// The New spool flow derived this tare, so the initial weight follows it.
// Handed to confirmParkTare() with the answer; a question closed without one
// parks nothing.
static bool  s_then_new = false;

bool isSpoolWeightScopeOpen() {
  return s_scope_popup != nullptr;
}

void closeSpoolWeightScope() {
  releaseScreen(&s_scope_popup);
}

// One screen's coordinates, inside the box.
#define SW_BOX_W     460
#define SW_X          10
#define SW_INNER_W   436
#define SW_TITLE_Y    10
#define SW_WARN_Y     38
#define SW_WARN_H     28
#define SW_ROWS_Y     40     // without the bag warning
#define SW_ROWS_Y_WARN 74
#define SW_ROW_H      52
#define SW_ROW_STEP   58
#define SW_BTN_H      46
#define SW_SUB_W     260

static void scopePicked(TareScope scope) {
  confirmParkTare(scope, s_tare_prompt_g, s_tare_spool_id, s_then_new);
  releaseScreen(&s_scope_popup);
}

struct ScopeRow { int title_id; const char *sub; uint8_t source; lv_event_cb_t cb; };

static void scopeRow(lv_obj_t *box, int y, const ScopeRow &r) {
  lv_obj_t *b = lv_btn_create(box);
  lv_obj_set_size(b, SW_INNER_W, SW_ROW_H);
  lv_obj_set_pos(b, SW_X, y);
  lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_ROW), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_ROW_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(UI_COL_ROW_PRESSED), 0);
  lv_obj_set_style_radius(b, UI_RADIUS_BTN, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_add_event_cb(b, r.cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *t = lv_label_create(b);
  lv_label_set_text(t, T(r.title_id));
  lv_obj_set_style_text_font(t, UI_FONT_BODY, 0);
  lv_obj_set_style_text_color(t, lv_color_hex(UI_COL_INK), 0);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 14, -9);
  lv_obj_t *sub = labelOneLine(b, r.sub, UI_FONT_CAPTION, SW_SUB_W);
  lv_obj_set_style_text_color(sub, lv_color_hex(UI_COL_CAPTION), 0);
  lv_obj_align(sub, LV_ALIGN_LEFT_MID, 14, 11);

  if (sm_tare_source != TARE_NONE && sm_tare_source == r.source) {
    char cur[32];
    snprintf(cur, sizeof(cur), T(STR_SW_CURRENT), (float)sm_spool_weight);
    lv_obj_t *c = lv_label_create(b);
    lv_label_set_text(c, cur);
    lv_obj_set_style_text_font(c, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(c, lv_color_hex(UI_COL_ACCENT), 0);
    lv_obj_align(c, LV_ALIGN_RIGHT_MID, -36, 0);
  }
  lv_obj_t *arr = lv_label_create(b);
  lv_label_set_text(arr, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_font(arr, UI_FONT_BODY, 0);
  lv_obj_set_style_text_color(arr, lv_color_hex(UI_COL_INK_FAINT), 0);
  lv_obj_align(arr, LV_ALIGN_RIGHT_MID, -14, 0);
}

// A bag on the pad is counted into the reading, and this number may be
// written to an entire brand. The device cannot tell whether one is there,
// so it says so rather than silently subtracting a weight that might not be
// on the scale at all.
static void scopeBagWarning(lv_obj_t *box) {
  lv_obj_t *strip = lv_obj_create(box);
  lv_obj_set_size(strip, SW_INNER_W, SW_WARN_H);
  lv_obj_set_pos(strip, SW_X, SW_WARN_Y);
  lv_obj_set_style_bg_color(strip, lv_color_hex(UI_COL_AMBER_BG), 0);
  lv_obj_set_style_border_color(strip, lv_color_hex(UI_COL_AMBER_LINE), 0);
  lv_obj_set_style_border_width(strip, 1, 0);
  lv_obj_set_style_radius(strip, UI_RADIUS_BTN, 0);
  lv_obj_set_style_pad_all(strip, 0, 0);
  lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
  char num[64], buf[72];
  snprintf(num, sizeof(num), T(STR_SPOOL_WEIGHT_BAG_HINT), bag_weight_g);
  snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING "  %s", num);
  lv_obj_t *l = lv_label_create(strip);
  lv_label_set_text(l, buf);
  lv_obj_set_style_text_font(l, UI_FONT_CAPTION, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_WARN), 0);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 10, 0);
}

// measured: the value is a reading off the pad, so a bag on it would be in
// there too. A typed value knows nothing of the bag.
static void showSpoolWeightPopup(float grams, bool then_new_spool, bool measured = true) {
  s_tare_prompt_g = grams;
  s_then_new = then_new_spool;
  // The typed value only gets here for the spool its entry was opened for,
  // see tare_entry.cpp.
  s_tare_spool_id = sm_id;

  // Not every backend has all three places to put it. BamBuddy has no
  // filament and no vendor object, only the spool's own core weight, which
  // its Spoolman proxy stores too (#2908). Rows for those are not built
  // rather than built and left to fail.
  const bool shared = backendCanTareFilamentOrVendor();
  char spool_sub[48], fil_sub[72];
  snprintf(spool_sub, sizeof(spool_sub), "#%d  %s", sm_id, sm_filament_name);
  snprintf(fil_sub, sizeof(fil_sub), "%s%s%s", sm_vendor_g, sm_vendor_g[0] ? " " : "", sm_filament_name);
  const ScopeRow rows[] = {
    { STR_SW_ONLY_SPOOL,   spool_sub,   TARE_SPOOL,    [](lv_event_t *e) { scopePicked(TARE_SCOPE_SPOOL); } },
    { STR_SW_ALL_FILAMENT, fil_sub,     TARE_FILAMENT, [](lv_event_t *e) { scopePicked(TARE_SCOPE_FILAMENT); } },
    { STR_SW_ALL_VENDOR,   sm_vendor_g, TARE_VENDOR,   [](lv_event_t *e) { scopePicked(TARE_SCOPE_VENDOR); } },
  };
  const bool shown[] = { backendCanTareSpool(), shared, shared };
  const int n = (shown[0] ? 1 : 0) + (shared ? 2 : 0);
  const bool warn = measured && bag_weight_g > 0.0f;
  const int y0 = warn ? SW_ROWS_Y_WARN : SW_ROWS_Y;
  const int cancel_y = y0 + n * SW_ROW_STEP;
  lv_obj_t *box = uiPopupBox(&s_scope_popup, SW_BOX_W, cancel_y + SW_BTN_H + 12);

  char title[64];
  snprintf(title, sizeof(title), T(STR_SW_SCOPE_TITLE), grams);
  lv_obj_t *t = labelOneLine(box, title, UI_FONT_BODY, SW_INNER_W);
  lv_obj_set_style_text_color(t, lv_color_hex(UI_COL_INK), 0);
  lv_obj_set_pos(t, SW_X + 4, SW_TITLE_Y);
  if (warn) scopeBagWarning(box);

  int y = y0;
  for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
    if (!shown[i]) continue;
    scopeRow(box, y, rows[i]);
    y += SW_ROW_STEP;
  }

  // Neutral like the weight popup's Close: nothing is lost by leaving, and
  // nothing is parked either.
  lv_obj_t *cancel = padButton(box, SW_X, cancel_y, SW_INNER_W, SW_BTN_H, UI_COL_ROW, UI_COL_ROW_PRESS_FILL);
  lv_obj_set_style_radius(cancel, UI_RADIUS_BTN, 0);
  padLabel(cancel, T(STR_CANCEL), UI_COL_INK_2, UI_FONT_SMALL);
  styleLeaveButton(cancel);
  lv_obj_add_event_cb(cancel, [](lv_event_t *e) {
    releaseScreen(&s_scope_popup);
  }, LV_EVENT_CLICKED, NULL);
}

void showSpoolWeightScope(float grams, bool measured, bool then_new_spool) {
  showSpoolWeightPopup(grams, then_new_spool, measured);
}
