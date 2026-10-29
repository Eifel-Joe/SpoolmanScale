#include "extras_chooser.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdint.h>

#include "app/app_state.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/spool_extras.h"
#include "theme.h"
#include "ui_common.h"

// The parts on the spool on the pad: up to three of the eight set up in the
// web settings, toggled here and remembered for the spool on OK.

static lv_obj_t *s_extras_popup = nullptr;
static uint8_t   s_extras_mask  = 0;
static lv_obj_t *s_extras_sum   = nullptr;
static lv_obj_t *s_extras_btn[SPOOL_EXTRAS_MAX] = {};
static void    (*s_on_close)()  = nullptr;

bool isExtrasChooserOpen() {
  return s_extras_popup != nullptr;
}

void closeExtrasChooser() {
  releaseScreen(&s_extras_popup);
  s_extras_sum = nullptr;
  for (int i = 0; i < SPOOL_EXTRAS_MAX; i++) s_extras_btn[i] = nullptr;
  s_on_close = nullptr;
}

static void extrasRefresh() {
  for (int i = 0; i < SPOOL_EXTRAS_MAX; i++) {
    if (!s_extras_btn[i]) continue;
    const bool on = s_extras_mask & (1u << i);
    lv_obj_set_style_bg_color(s_extras_btn[i], lv_color_hex(on ? UI_COL_OK_BG : UI_COL_CHIP), 0);
    lv_obj_set_style_border_width(s_extras_btn[i], on ? 2 : 0, 0);
    lv_obj_set_style_border_color(s_extras_btn[i], lv_color_hex(UI_COL_OK_BG_PRESSED), 0);
    lv_obj_t *l = lv_obj_get_child(s_extras_btn[i], 0);
    if (l) lv_obj_set_style_text_color(l, lv_color_hex(on ? UI_COL_OK_TEXT : UI_COL_INK_2), 0);
  }
  if (s_extras_sum) {
    char buf[48];
    snprintf(buf, sizeof(buf), T(STR_EXTRAS_SUM), spoolExtrasGrams(s_extras_mask));
    lv_label_set_text(s_extras_sum, buf);
  }
}

static void extrasClose(bool keep) {
  if (keep && sm_found && sm_id > 0) {
    spoolExtrasSetFor(sm_id, s_extras_mask);
    char names[96];
    spoolExtrasNames(s_extras_mask, names, sizeof(names));
    logSDf("UI: spool %d parts -> %s (%.1f g)", sm_id, names[0] ? names : "none",
           spoolExtrasGrams(s_extras_mask));
  }
  // Deleted on the next pass: this runs inside one of the popup's buttons.
  if (s_extras_popup) { lv_obj_del_async(s_extras_popup); s_extras_popup = nullptr; }
  s_extras_sum = nullptr;
  for (int i = 0; i < SPOOL_EXTRAS_MAX; i++) s_extras_btn[i] = nullptr;
  // Back to where the chooser was opened from.
  if (s_on_close) s_on_close();
}

void showExtrasChooser(void (*on_close)()) {
  s_on_close = on_close;
  logSD("SHOW: ExtrasChooser");
  releaseScreen(&s_extras_popup);
  s_extras_mask = spoolExtrasFor(sm_found ? sm_id : 0);

  lv_obj_t *scrim = lv_obj_create(lv_scr_act());
  s_extras_popup = scrim;
  lv_obj_set_size(scrim, 480, 320);
  lv_obj_set_pos(scrim, 0, 0);
  lv_obj_set_style_bg_color(scrim, lv_color_hex(UI_COL_SCRIM), 0);
  lv_obj_set_style_bg_opa(scrim, UI_OPA_SCRIM, 0);
  lv_obj_set_style_border_width(scrim, 0, 0);
  lv_obj_set_style_radius(scrim, 0, 0);
  lv_obj_set_style_pad_all(scrim, 0, 0);
  lv_obj_clear_flag(scrim, LV_OBJ_FLAG_SCROLLABLE);

  // 2 columns of up to 4 parts, the sum above them, OK and Cancel below.
  const int BOX_W = 460, EDGE = 10, GAP = 6, ITEM_H = 40, FOOT_H = UI_TOUCH_MIN;
  const int GRID_Y = 52, ROWS = SPOOL_EXTRAS_MAX / 2;
  const int FOOT_Y = GRID_Y + ROWS * ITEM_H + (ROWS - 1) * GAP + 2 * GAP;
  const int BOX_H = FOOT_Y + FOOT_H + EDGE;
  const int COL_W = (BOX_W - 2 * EDGE - GAP) / 2;
  lv_obj_t *box = lv_obj_create(scrim);
  lv_obj_set_size(box, BOX_W, BOX_H);
  lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(box, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_border_color(box, lv_color_hex(UI_COL_POPUP_BORDER), 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_radius(box, UI_RADIUS_BOX, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(box);
  lv_label_set_text(title, T(STR_EXTRAS_TITLE));
  lv_obj_set_style_text_color(title, lv_color_hex(UI_COL_INK), 0);
  lv_obj_set_style_text_font(title, UI_FONT_BODY, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

  s_extras_sum = lv_label_create(box);
  lv_obj_set_style_text_color(s_extras_sum, lv_color_hex(UI_COL_WARN), 0);
  lv_obj_set_style_text_font(s_extras_sum, UI_FONT_SMALL, 0);
  lv_obj_align(s_extras_sum, LV_ALIGN_TOP_MID, 0, 28);

  int shown = 0;
  for (int i = 0; i < SPOOL_EXTRAS_MAX; i++) {
    s_extras_btn[i] = nullptr;
    if (!spoolExtraUsed(i)) continue;
    const int col = shown % 2, row = shown / 2;
    shown++;
    lv_obj_t *b = lv_btn_create(box);
    s_extras_btn[i] = b;
    lv_obj_set_size(b, COL_W, ITEM_H);
    lv_obj_set_pos(b, EDGE + col * (COL_W + GAP), GRID_Y + row * (ITEM_H + GAP));
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_PRESS_FILL), LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, UI_RADIUS_BTN, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    char buf[40];
    snprintf(buf, sizeof(buf), "%s  %.1f g", spoolExtraAt(i).name, spoolExtraAt(i).grams);
    lv_label_set_text(l, buf);
    lv_obj_set_style_text_font(l, UI_FONT_SMALL, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, COL_W - 12);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, [](lv_event_t *e) {
      const int i = (int)(intptr_t)lv_event_get_user_data(e);
      const uint8_t bit = (uint8_t)(1u << i);
      if (s_extras_mask & bit) {
        s_extras_mask &= (uint8_t)~bit;
      } else {
        int picked = 0;
        for (int k = 0; k < SPOOL_EXTRAS_MAX; k++) picked += (s_extras_mask >> k) & 1;
        if (picked >= SPOOL_EXTRAS_PICK) return;   // three at most; untick one first
        s_extras_mask |= bit;
      }
      extrasRefresh();
    }, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }

  const int BTN_W = (BOX_W - 2 * EDGE - GAP) / 2;
  lv_obj_t *cancel = lv_btn_create(box);
  lv_obj_set_size(cancel, BTN_W, FOOT_H);
  lv_obj_set_pos(cancel, EDGE, FOOT_Y);
  lv_obj_set_style_radius(cancel, UI_RADIUS_BTN, 0);
  lv_obj_set_style_shadow_width(cancel, 0, 0);
  lv_obj_t *lc = lv_label_create(cancel);
  lv_label_set_text(lc, T(STR_CANCEL));
  lv_obj_set_style_text_font(lc, UI_FONT_SMALL, 0);
  lv_obj_center(lc);
  // The original's way out: red outline and a cross in front. The style sets
  // the fill and the caption colour itself, once the caption is on the button.
  styleLeaveButton(cancel);
  lv_obj_add_event_cb(cancel, [](lv_event_t *e) { extrasClose(false); }, LV_EVENT_CLICKED, NULL);

  lv_obj_t *ok = lv_btn_create(box);
  lv_obj_set_size(ok, BTN_W, FOOT_H);
  lv_obj_set_pos(ok, EDGE + BTN_W + GAP, FOOT_Y);
  lv_obj_set_style_bg_color(ok, lv_color_hex(UI_COL_OK_BG), 0);
  lv_obj_set_style_bg_color(ok, lv_color_hex(UI_COL_OK_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(ok, UI_RADIUS_BTN, 0);
  lv_obj_set_style_shadow_width(ok, 0, 0);
  lv_obj_t *lo = lv_label_create(ok);
  lv_label_set_text(lo, T(STR_BTN_OK));
  lv_obj_set_style_text_color(lo, lv_color_hex(UI_COL_OK_TEXT), 0);
  lv_obj_set_style_text_font(lo, UI_FONT_SMALL, 0);
  lv_obj_center(lo);
  lv_obj_add_event_cb(ok, [](lv_event_t *e) { extrasClose(true); }, LV_EVENT_CLICKED, NULL);

  extrasRefresh();
}
