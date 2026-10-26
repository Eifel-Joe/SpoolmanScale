#include "ui/sort_twin_popup.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>

#include "hardware/sd_logger.h"
#include "lang.h"
#include "ui/theme.h"
#include "ui/ui_common.h"

// The box: a title, the question, two answers in the card's answer row.
#define STP_TITLE_Y    16
#define STP_TEXT_Y     64
#define STP_TEXT_W     (UI_POPUP_W - UI_CARD_TEXT_PAD)
#define STP_GAP         8
#define STP_BTN_W      ((UI_POPUP_W - 2 * UI_CARD_ROW_X - STP_GAP) / 2)

enum StpAnswer : uint8_t { STP_NONE = 0, STP_NEW, STP_TAKE };

static lv_obj_t*  s_box_scr = nullptr;
static void     (*s_done)(bool) = nullptr;
static StpAnswer  s_answer = STP_NONE;

bool sortTwinPopupOpen() { return s_box_scr != nullptr; }

void closeSortTwinPopup() {
  releaseScreen(&s_box_scr);
  s_answer = STP_NONE;
}

static void park(lv_event_t* e) {
  s_answer = (StpAnswer)(intptr_t)lv_event_get_user_data(e);
  logSDf("BTN: SortTwin -> %s", s_answer == STP_TAKE ? "take it" : "create new");
  // Hidden here, deleted by the loop: this is the button's own callback.
  if (s_box_scr) lv_obj_add_flag(s_box_scr, LV_OBJ_FLAG_HIDDEN);
}

static void answer(lv_obj_t* box, int x, bool take) {
  lv_obj_t* btn = lv_btn_create(box);
  lv_obj_set_size(btn, STP_BTN_W, UI_POPUP_BTN_H);
  lv_obj_set_pos(btn, x, UI_CARD_ROW_Y);
  lv_obj_set_style_bg_color(btn, lv_color_hex(take ? UI_COL_OK_BG : UI_COL_ROW), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(take ? UI_COL_OK_BG_PRESSED : UI_COL_ROW_PRESS_FILL),
                            LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn, take ? 0 : 1, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(UI_COL_LINE), 0);
  lv_obj_set_style_radius(btn, UI_RADIUS_BTN, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_add_event_cb(btn, park, LV_EVENT_CLICKED, (void*)(intptr_t)(take ? STP_TAKE : STP_NEW));
  lv_obj_t* l = lv_label_create(btn);
  lv_label_set_text(l, T(take ? STR_SORT_TWIN_TAKE : STR_SORT_TWIN_NEW));
  lv_obj_set_style_text_color(l, lv_color_hex(take ? UI_COL_OK_TEXT : UI_COL_INK), 0);
  lv_obj_set_style_text_font(l, UI_FONT_BODY, 0);
  lv_obj_center(l);
}

static lv_obj_t* buildBox() {
  s_box_scr = lv_obj_create(lv_scr_act());
  lv_obj_set_size(s_box_scr, LV_HOR_RES, LV_VER_RES);
  lv_obj_set_pos(s_box_scr, 0, 0);
  lv_obj_set_style_bg_color(s_box_scr, lv_color_hex(UI_COL_SCRIM), 0);
  lv_obj_set_style_bg_opa(s_box_scr, UI_OPA_SCRIM, 0);
  lv_obj_set_style_border_width(s_box_scr, 0, 0);
  lv_obj_set_style_radius(s_box_scr, 0, 0);
  lv_obj_set_style_pad_all(s_box_scr, 0, 0);
  lv_obj_clear_flag(s_box_scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* box = lv_obj_create(s_box_scr);
  lv_obj_set_size(box, UI_POPUP_W, UI_CARD_H);
  lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(box, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_border_color(box, lv_color_hex(UI_COL_WARN), 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_radius(box, UI_RADIUS_BOX, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  return box;
}

// A centred line of text, wrapping inside the card's text width.
static void textLine(lv_obj_t* box, const char* text, const lv_font_t* font, int y) {
  lv_obj_t* l = lv_label_create(box);
  lv_label_set_text(l, text);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, STP_TEXT_W);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_INK), 0);
}

void showSortTwinPopup(const char* name, void (*done)(bool take)) {
  logSDf("SHOW: SortTwin \"%s\"", name ? name : "");
  closeSortTwinPopup();
  s_done = done;
  lv_obj_t* box = buildBox();
  textLine(box, T(STR_SORT_TWIN_TITLE), UI_FONT_HEADLINE, STP_TITLE_Y);
  // The name is the inventory's, up to a filament field's 63 bytes.
  char text[220];
  snprintf(text, sizeof(text), T(STR_SORT_TWIN_TEXT), name ? name : "");
  textLine(box, text, UI_FONT_BODY, STP_TEXT_Y);
  answer(box, UI_CARD_ROW_X, true);
  answer(box, UI_CARD_ROW_X + STP_BTN_W + STP_GAP, false);
}

void sortTwinPopupTick() {
  if (s_answer == STP_NONE) return;
  const bool take = s_answer == STP_TAKE;
  void (*done)(bool) = s_done;
  closeSortTwinPopup();
  if (done) done(take);
}
