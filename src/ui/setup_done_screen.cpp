#include "setup_done_screen.h"
#include "navigation.h"
#include "app/app_state.h"
#include "app/deferred_actions.h"

#include <Arduino.h>
#include <lvgl.h>
#include <cstring>

#include "app_config.h"
#include "factor_screen.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/backend.h"
#include "services/user_options.h"
#include "setup_frame.h"
#include "ui_common.h"
#include "theme.h"

#define DONE_CARD_X       12
#define DONE_CARD_Y       54
#define DONE_CARD_W      456
#define DONE_ROW_H        46
#define DONE_ROW_H_SUB    56   // a row with a second line under its title
#define DONE_ROW_INSET    14
#define DONE_DISC         24
#define DONE_TEXT_X       52
#define DONE_VALUE_W     200
#define DONE_HINT_Y      226

// One line of the summary: done, or still to do with where to do it.
struct DoneRow {
  bool        ok;
  const char *title;
  const char *value;   // right, quiet; nullptr for none
  const char *sub;     // under the title; nullptr for none
};

static int addDoneRow(lv_obj_t *card, int y, const DoneRow &r, bool last) {
  const int h = r.sub ? DONE_ROW_H_SUB : DONE_ROW_H;
  lv_obj_t *row = lv_obj_create(card);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, DONE_CARD_W - 2, h);
  lv_obj_set_pos(row, 0, y);
  if (!last) {
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(UI_COL_LINE_SOFT), 0);
  }

  lv_obj_t *disc = lv_obj_create(row);
  lv_obj_remove_style_all(disc);
  lv_obj_set_size(disc, DONE_DISC, DONE_DISC);
  lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(disc, lv_color_hex(r.ok ? UI_COL_PICKED_BG : UI_COL_AMBER_BG), 0);
  lv_obj_align(disc, LV_ALIGN_LEFT_MID, DONE_ROW_INSET, 0);
  lv_obj_t *mark = lv_label_create(disc);
  lv_label_set_text(mark, r.ok ? LV_SYMBOL_OK : LV_SYMBOL_WARNING);
  lv_obj_set_style_text_font(mark, UI_FONT_CAPTION, 0);
  lv_obj_set_style_text_color(mark, lv_color_hex(r.ok ? UI_COL_ACCENT : UI_COL_WARN), 0);
  lv_obj_center(mark);

  lv_obj_t *t = lv_label_create(row);
  lv_label_set_text(t, r.title);
  lv_obj_set_style_text_font(t, UI_FONT_BODY, 0);
  lv_obj_set_style_text_color(t, lv_color_hex(UI_COL_INK), 0);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, DONE_TEXT_X, r.sub ? -9 : 0);
  if (r.sub) {
    lv_obj_t *s = lv_label_create(row);
    lv_label_set_text(s, r.sub);
    lv_obj_set_style_text_font(s, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(s, lv_color_hex(UI_COL_INK_SOFT), 0);
    lv_obj_align(s, LV_ALIGN_LEFT_MID, DONE_TEXT_X, 11);
  }
  if (r.value) {
    lv_obj_t *v = lv_label_create(row);
    lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
    lv_obj_set_size(v, DONE_VALUE_W, lv_font_get_line_height(UI_FONT_SMALL));
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(v, r.value);
    lv_obj_set_style_text_font(v, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(v, lv_color_hex(UI_COL_INK_SOFT), 0);
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, -DONE_ROW_INSET, 0);
  }
  return y + h;
}

// FilaMan cannot work without its key and token. BamBuddy's key may stay
// empty - an instance without authentication answers without it - unless
// the address test met one that asked for it.
static bool credentialsMissing() {
  if (backendIsFilaMan())
    return filamanApiKey()[0] == '\0' || filamanDeviceToken()[0] == '\0';
  return backendIsBamBuddy() && setupKeyRequired() && bambuddyApiKey()[0] == '\0';
}

// Under the backend row: what is still missing, if anything.
static const char *backendRowSub() {
  if (setupServerLater())   return T(STR_SP_NO_ANSWER);
  if (credentialsMissing()) return T(STR_SETUP_DONE_CREDS);
  return nullptr;
}

static lv_obj_t *buildDoneCard(lv_obj_t *scr) {
  lv_obj_t *card = lv_obj_create(scr);
  lv_obj_set_pos(card, DONE_CARD_X, DONE_CARD_Y);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_style_radius(card, UI_RADIUS_ROW, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(UI_COL_LINE_SOFT), 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);

  char path[96];
  snprintf(path, sizeof(path), T(STR_SETUP_DONE_PATH),
           T(STR_SETTINGS_TITLE), T(STR_SCALE_TITLE), T(STR_BTN_CALIBRATE));
  char factor[32];
  snprintf(factor, sizeof(factor), T(STR_CAL_FACTOR_SHORT), cal_factor);
  const bool calibrated = cal_factor != CAL_FACTOR_DEFAULT;

  int y = 0;
  y = addDoneRow(card, y, { wifi_ok, T(STR_SETUP_TILE_WIFI), cfg_wifi_ssid, nullptr }, false);
  y = addDoneRow(card, y, { backendRowSub() == nullptr, backendName(), backendHost(),
                            backendRowSub() },
                 !g_scale_fitted);
  if (g_scale_fitted) {
    y = addDoneRow(card, y, { calibrated,
                              calibrated ? T(STR_SETUP_TILE_SCALE) : T(STR_SETUP_DONE_UNCAL),
                              calibrated ? factor : nullptr,
                              calibrated ? nullptr : path }, true);
  }
  lv_obj_set_size(card, DONE_CARD_W, y + 2);
  return card;
}

void showSetupDoneScreen() {
  logSD("SHOW: SetupDoneScreen");
  closeFactorScreen();
  releaseSetupScreens();
  hideAllOverlays();
  releaseScreen(&scr_setup_done);
  scr_setup_done = buildOverlayScreen();
  setupHeader(scr_setup_done, T(STR_SETUP_DONE_TITLE), SETUP_STEP_NONE, nullptr);
  buildDoneCard(scr_setup_done);

  lv_obj_t *hint = setupCenteredLabel(scr_setup_done, T(STR_SETUP_DONE_HINT), UI_FONT_SMALL,
                                      DONE_CARD_W);
  lv_obj_set_style_text_color(hint, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_pos(hint, DONE_CARD_X, DONE_HINT_Y);

  setupActionBar(scr_setup_done, nullptr, nullptr, T(STR_SETUP_GO), [](lv_event_t *e) {
    logSD("BTN: Setup done -> Main");
    // Deferred: the main screen deletes this one.
    finish_setup_pending = true;
  });
  lv_obj_clear_flag(scr_setup_done, LV_OBJ_FLAG_HIDDEN);
}
