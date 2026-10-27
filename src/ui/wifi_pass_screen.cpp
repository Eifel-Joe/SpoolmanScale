#include "wifi_setup_screen.h"
#include "navigation.h"
#include "app/app_state.h"

#include <Arduino.h>
#include <lvgl.h>
#include <cstring>

#include "hardware/sd_logger.h"
#include "lang.h"
#include "setup_frame.h"
#include "ui_common.h"
#include "theme.h"

// The password step of the WiFi setup: the field, a button that shows or
// hides what was typed, and the keyboard. The list and the connect live in
// wifi_setup_screen.cpp and wifi_connect_view.cpp.

#define WIFI_PASS_Y          52
#define WIFI_PASS_X          12
#define WIFI_PASS_W         400
#define WIFI_PASS_H          40
#define WIFI_PASS_BORDER      2
#define WIFI_EYE_X          420
#define WIFI_EYE_W           48
#define WIFI_KB_H           216
#define WIFI_KB_GAP           4

// LVGL's default keyboard, adjusted in two places. The special page had no
// ^ ~ | or `, so a WiFi password containing one could not be typed at all.
// And the keyboard key at the bottom left only sends CANCEL, which nothing on
// this screen answers, so it is gone on all three pages and the space bar has
// its width. Otherwise the same keys, and the control keys carry LVGL's own
// labels, so lv_keyboard_def_event_cb() still switches pages, deletes and
// confirms. LVGL keeps the arrays by pointer, which is why they live here and
// not on the stack.
#define WIFI_KB_BTN(w)  (LV_BTNMATRIX_CTRL_POPOVER | (w))
#define WIFI_KB_KEY     WIFI_KB_BTN(1)
// One bottom row for all three pages, so nothing shifts on a page switch.
#define WIFI_KB_MAP_BOTTOM   LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
// The confirm key is twice as wide as the arrows and drawn in the accent
// (wifiKbDrawCb), so the way on reads as the main button.
#define WIFI_KB_CTRL_BOTTOM  LV_BTNMATRIX_CTRL_CHECKED | 2, 6, \
                             LV_BTNMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 4
static const char * const wifi_kb_map_lc[] = {
  "1#", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
  "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_NEW_LINE, "\n",
  "_", "-", "z", "x", "c", "v", "b", "n", "m", ".", ",", ":", "\n",
  WIFI_KB_MAP_BOTTOM
};
static const char * const wifi_kb_map_uc[] = {
  "1#", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", LV_SYMBOL_BACKSPACE, "\n",
  "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", LV_SYMBOL_NEW_LINE, "\n",
  "_", "-", "Z", "X", "C", "V", "B", "N", "M", ".", ",", ":", "\n",
  WIFI_KB_MAP_BOTTOM
};
// Lower and upper case share their widths.
static const lv_btnmatrix_ctrl_t wifi_kb_ctrl_text[] = {
  LV_KEYBOARD_CTRL_BTN_FLAGS | 5,
  WIFI_KB_BTN(4), WIFI_KB_BTN(4), WIFI_KB_BTN(4), WIFI_KB_BTN(4), WIFI_KB_BTN(4),
  WIFI_KB_BTN(4), WIFI_KB_BTN(4), WIFI_KB_BTN(4), WIFI_KB_BTN(4), WIFI_KB_BTN(4),
  LV_BTNMATRIX_CTRL_CHECKED | 7,
  LV_KEYBOARD_CTRL_BTN_FLAGS | 6,
  WIFI_KB_BTN(3), WIFI_KB_BTN(3), WIFI_KB_BTN(3), WIFI_KB_BTN(3), WIFI_KB_BTN(3),
  WIFI_KB_BTN(3), WIFI_KB_BTN(3), WIFI_KB_BTN(3), WIFI_KB_BTN(3),
  LV_BTNMATRIX_CTRL_CHECKED | 7,
  LV_BTNMATRIX_CTRL_CHECKED | WIFI_KB_KEY, LV_BTNMATRIX_CTRL_CHECKED | WIFI_KB_KEY,
  WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY,
  LV_BTNMATRIX_CTRL_CHECKED | WIFI_KB_KEY, LV_BTNMATRIX_CTRL_CHECKED | WIFI_KB_KEY,
  LV_BTNMATRIX_CTRL_CHECKED | WIFI_KB_KEY,
  WIFI_KB_CTRL_BOTTOM
};
static const char * const wifi_kb_map_spec[] = {
  "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "^", "~", LV_SYMBOL_BACKSPACE, "\n",
  "abc", "+", "&", "/", "*", "=", "%", "!", "?", "#", "<", ">", "\n",
  "\\", "@", "$", "(", ")", "{", "}", "[", "]", ";", "\"", "'", "|", "`", "\n",
  WIFI_KB_MAP_BOTTOM
};
static const lv_btnmatrix_ctrl_t wifi_kb_ctrl_spec[] = {
  WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY,
  WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY,
  LV_BTNMATRIX_CTRL_CHECKED | 2,
  LV_KEYBOARD_CTRL_BTN_FLAGS | 2,
  WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY,
  WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY,
  WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY,
  WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY, WIFI_KB_KEY,
  WIFI_KB_CTRL_BOTTOM
};

static lv_obj_t *ta_wifi_pass = nullptr;
static lv_obj_t *kb_wifi_pass = nullptr;
static lv_obj_t *lbl_wifi_eye = nullptr;

// ✓ reports READY on the keyboard and then on the textarea as well, so this
// can run twice for one press; the second run only repeats the first.
static void wifiPassReadyCb(lv_event_t *e) {
  if (!ta_wifi_pass) return;
  // Copied now, connected on the next loop pass: the NVS write and the
  // ten second connect attempt do not belong in the keyboard's callback,
  // and the textarea is deleted with this screen before they run.
  wifiSetupSubmitPassword(lv_textarea_get_text(ta_wifi_pass));
}

// The confirm key in the accent. A button matrix has one style for all its
// keys, so the one key is coloured as it is drawn.
static void wifiKbDrawCb(lv_event_t *e) {
  lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
  if (dsc->part != LV_PART_ITEMS || !dsc->rect_dsc) return;
  const char *txt = lv_btnmatrix_get_btn_text(lv_event_get_target(e), dsc->id);
  if (!txt || strcmp(txt, LV_SYMBOL_OK) != 0) return;
  dsc->rect_dsc->bg_color = lv_color_hex(UI_COL_ACCENT);
  if (dsc->label_dsc) dsc->label_dsc->color = lv_color_hex(UI_COL_ON_ACCENT);
}

// Shown in plain text by default, as before: a typo is the common failure on
// a touch keyboard. The eye hides it for typing with someone watching.
static void wifiEyeCb(lv_event_t *e) {
  if (!ta_wifi_pass || !lbl_wifi_eye) return;
  const bool hide = !lv_textarea_get_password_mode(ta_wifi_pass);
  lv_textarea_set_password_mode(ta_wifi_pass, hide);
  lv_label_set_text(lbl_wifi_eye, hide ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
}

static void styleWifiKeyboard(lv_obj_t *kb) {
  lv_obj_set_style_bg_color(kb, lv_color_hex(UI_COL_GROUND), 0);
  lv_obj_set_style_border_width(kb, 0, 0);
  lv_obj_set_style_pad_all(kb, WIFI_KB_GAP, 0);
  lv_obj_set_style_pad_gap(kb, WIFI_KB_GAP, 0);
  lv_obj_set_style_bg_color(kb, lv_color_hex(UI_COL_ROW), LV_PART_ITEMS);
  lv_obj_set_style_text_color(kb, lv_color_hex(UI_COL_INK), LV_PART_ITEMS);
  lv_obj_set_style_text_font(kb, UI_FONT_BODY, LV_PART_ITEMS);
  lv_obj_set_style_radius(kb, UI_RADIUS_INPUT, LV_PART_ITEMS);
  lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
  // The control keys - page switch, delete, arrows - one step quieter.
  lv_obj_set_style_bg_color(kb, lv_color_hex(UI_COL_SURFACE), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(kb, lv_color_hex(UI_COL_INK_SOFT), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(kb, lv_color_hex(UI_COL_PRESS_FILL), LV_PART_ITEMS | LV_STATE_PRESSED);
}

void showWifiPassScreen() {
  logSD("SHOW: WifiPassScreen");
  logSD("UI: Screen -> WifiPass");
  hideAllOverlays();
  releaseScreen(&scr_wifi_pass);
  ta_wifi_pass = nullptr;
  kb_wifi_pass = nullptr;
  lbl_wifi_eye = nullptr;
  buildWifiPassScreen();
  lv_obj_clear_flag(scr_wifi_pass, LV_OBJ_FLAG_HIDDEN);
}

static void buildWifiPassField(lv_obj_t *scr) {
  ta_wifi_pass = lv_textarea_create(scr);
  lv_textarea_set_one_line(ta_wifi_pass, true);
  lv_textarea_set_password_mode(ta_wifi_pass, false);
  lv_textarea_set_placeholder_text(ta_wifi_pass, T(STR_WIFI_PASS_PLACEHOLDER));
  lv_textarea_set_max_length(ta_wifi_pass, WIFI_PASS_MAX_LEN);
  lv_obj_set_size(ta_wifi_pass, WIFI_PASS_W, WIFI_PASS_H);
  lv_obj_set_pos(ta_wifi_pass, WIFI_PASS_X, WIFI_PASS_Y);
  lv_obj_set_scrollbar_mode(ta_wifi_pass, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_text_font(ta_wifi_pass, UI_FONT_BODY, 0);
  // The default theme pads a textarea far more than 40 px leave for a 16 px
  // line, so every new character scrolled the field up and down to keep the
  // cursor in view. The line is centred in what the border leaves instead,
  // and only a long password scrolls, sideways.
  const int pad_ver = (WIFI_PASS_H - 2 * WIFI_PASS_BORDER - lv_font_get_line_height(UI_FONT_BODY)) / 2;
  lv_obj_set_style_pad_ver(ta_wifi_pass, pad_ver, 0);
  lv_obj_set_scroll_dir(ta_wifi_pass, LV_DIR_HOR);
  lv_obj_set_style_text_color(ta_wifi_pass, lv_color_hex(UI_COL_INK_MAX), 0);
  // LVGL's default theme greys the placeholder for a dark ground on every
  // palette (theme.cpp), 1.7 to 2.2:1 on this fill; the quiet text colour
  // reads at 3.7 to 5.4:1.
  lv_obj_set_style_text_color(ta_wifi_pass, lv_color_hex(UI_COL_INK_SOFT), LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_bg_color(ta_wifi_pass, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_radius(ta_wifi_pass, UI_RADIUS_ROW, 0);
  lv_obj_set_style_border_width(ta_wifi_pass, WIFI_PASS_BORDER, 0);
  lv_obj_set_style_border_color(ta_wifi_pass, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_border_color(ta_wifi_pass, lv_color_hex(UI_COL_ACCENT), LV_STATE_FOCUSED);

  lv_obj_t *eye = lv_btn_create(scr);
  lv_obj_set_size(eye, WIFI_EYE_W, WIFI_PASS_H);
  lv_obj_set_pos(eye, WIFI_EYE_X, WIFI_PASS_Y);
  lv_obj_set_style_radius(eye, UI_RADIUS_ROW, 0);
  lv_obj_set_style_shadow_width(eye, 0, 0);
  lv_obj_set_style_bg_color(eye, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_bg_color(eye, lv_color_hex(UI_COL_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(eye, 1, 0);
  lv_obj_set_style_border_color(eye, lv_color_hex(UI_COL_LINE_SOFT), 0);
  lv_obj_add_event_cb(eye, wifiEyeCb, LV_EVENT_CLICKED, NULL);
  lbl_wifi_eye = lv_label_create(eye);
  lv_label_set_text(lbl_wifi_eye, LV_SYMBOL_EYE_CLOSE);
  lv_obj_set_style_text_font(lbl_wifi_eye, UI_FONT_BODY, 0);
  lv_obj_set_style_text_color(lbl_wifi_eye, lv_color_hex(UI_COL_INK_SOFT), 0);
  lv_obj_center(lbl_wifi_eye);
}

void buildWifiPassScreen() {
  logSD("BUILD: WifiPassScreen");
  releaseScreen(&scr_wifi_pass);
  scr_wifi_pass = buildOverlayScreen();

  char title[64];
  snprintf(title, sizeof(title), T(STR_WIFI_PASS_HINT), wifiSetupChosenSsid());
  setupOrSettingsHeader(scr_wifi_pass, title, SETUP_STEP_WIFI,
                        [](lv_event_t *e) { showWifiSetupScreen(); });

  buildWifiPassField(scr_wifi_pass);

  kb_wifi_pass = lv_keyboard_create(scr_wifi_pass);
  lv_keyboard_set_map(kb_wifi_pass, LV_KEYBOARD_MODE_TEXT_LOWER,
                      (const char **)wifi_kb_map_lc, wifi_kb_ctrl_text);
  lv_keyboard_set_map(kb_wifi_pass, LV_KEYBOARD_MODE_TEXT_UPPER,
                      (const char **)wifi_kb_map_uc, wifi_kb_ctrl_text);
  lv_keyboard_set_map(kb_wifi_pass, LV_KEYBOARD_MODE_SPECIAL,
                      (const char **)wifi_kb_map_spec, wifi_kb_ctrl_spec);
  lv_keyboard_set_textarea(kb_wifi_pass, ta_wifi_pass);
  lv_obj_set_size(kb_wifi_pass, 480, WIFI_KB_H);
  lv_obj_align(kb_wifi_pass, LV_ALIGN_BOTTOM_MID, 0, 0);
  styleWifiKeyboard(kb_wifi_pass);
  lv_obj_add_event_cb(kb_wifi_pass, wifiKbDrawCb, LV_EVENT_DRAW_PART_BEGIN, NULL);

  // Both confirm keys connect. ✓ reports READY on the keyboard, ↵ only on the
  // textarea, so the handler sits on both.
  lv_obj_add_event_cb(kb_wifi_pass, wifiPassReadyCb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(ta_wifi_pass, wifiPassReadyCb, LV_EVENT_READY, NULL);
}
