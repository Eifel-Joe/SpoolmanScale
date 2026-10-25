#include "empty_spool_type.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app/app_state.h"
#include "hardware/sd_logger.h"
#include "services/spool_extras.h"
#include "services/spool_type_rules.h"
#include "services/spool_types.h"
#include "services/user_options.h"
#include "spool_type_name.h"
#include "theme.h"
#include "ui_common.h"
#include "weight_format.h"
#include "lang.h"   // T(), last: its macro T(id) is a name other headers may use

static lv_obj_t *s_scr    = nullptr;
static lv_obj_t *s_weight = nullptr;   // the live number
static lv_obj_t *s_note   = nullptr;   // what to do, or what was just added
static lv_obj_t *s_next   = nullptr;
static lv_obj_t *s_part[SPOOL_EXTRAS_MAX] = {};

// The parts ticked for this weighing: the default at the start, never stored.
static uint8_t s_mask = 0;
// A name entry opened from here is up, and this screen is hidden under it.
static bool s_naming = false;
// What the weight line and Next show, so the loop redraws only on a change.
static bool s_drawn         = false;
static char s_drawn_text[24] = "";
static bool s_drawn_ok      = false;

bool isEmptySpoolTypeOpen() {
  return s_scr != nullptr;
}

void closeEmptySpoolType() {
  if (s_naming) closeSpoolTypeName();   // the name entry opened from here
  s_naming = false;
  // Deleted on the next timer pass: the back button lands here from inside
  // its own event.
  releaseScreen(&s_scr);
  s_weight = nullptr;
  s_note   = nullptr;
  s_next   = nullptr;
  for (int i = 0; i < SPOOL_EXTRAS_MAX; i++) s_part[i] = nullptr;
  s_drawn = false;
}

// The reading without the parts ticked here. Not spoolExtrasBareReading():
// that takes the choice of the spool on the pad, and this one has no entry.
static float emptyBare() {
  return extrasBare(scale_weight_g, spoolExtrasGrams(s_mask));
}

// The same look as the parts chooser of the weight popup (extras_chooser.cpp).
static void emptyShowParts() {
  for (int i = 0; i < SPOOL_EXTRAS_MAX; i++) {
    lv_obj_t *b = s_part[i];
    if (!b) continue;
    const bool on = s_mask & (1u << i);
    lv_obj_set_style_bg_color(b, lv_color_hex(on ? UI_COL_OK_BG : UI_COL_CHIP), 0);
    lv_obj_set_style_border_width(b, on ? 2 : 0, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(UI_COL_OK_BG_PRESSED), 0);
    lv_obj_t *l = lv_obj_get_child(b, 0);
    if (l) lv_obj_set_style_text_color(l, lv_color_hex(on ? UI_COL_OK_TEXT : UI_COL_INK_2), 0);
  }
}

// The weight line and the look of Next. Next counts only for a weight an
// empty spool can have (spoolTypeTarePlausible); otherwise it is greyed and
// its press ignored, as on the tare keypad.
static void emptyShowWeight(bool force) {
  if (!s_weight || !s_next) return;
  const float bare = emptyBare();
  const bool ok = scale_ready && spoolTypeTarePlausible(bare);
  // Whole grams or tenths, as the user set it for every weight on the panel.
  char buf[sizeof(s_drawn_text)];
  if (scale_ready) fmtG(buf, sizeof(buf), bare);
  else             snprintf(buf, sizeof(buf), "--- g");
  if (!force && s_drawn && ok == s_drawn_ok && strcmp(buf, s_drawn_text) == 0) return;
  s_drawn    = true;
  s_drawn_ok = ok;
  snprintf(s_drawn_text, sizeof(s_drawn_text), "%s", buf);

  lv_label_set_text(s_weight, buf);
  lv_obj_set_style_text_color(s_weight, lv_color_hex(ok ? UI_COL_INK : UI_COL_CAPTION), 0);
  lv_obj_set_style_bg_color(s_next, lv_color_hex(ok ? UI_COL_OK_BG : UI_COL_SURFACE), 0);
  lv_obj_set_style_text_color(lv_obj_get_child(s_next, 0),
                              lv_color_hex(ok ? UI_COL_OK_TEXT : UI_COL_CAPTION), 0);
}

// From spoolTypeNameTick(), in the loop: the type is in the catalog. This
// screen comes back with a line saying so and waits for the next spool.
static void emptyTypeCreated(float grams, const char *name) {
  s_naming = false;
  if (!s_scr) return;
  lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_HIDDEN);
  // Room for the longest name whole (SPOOL_TYPE_NAME_MAX - 1 bytes) and the
  // text around it, so nothing is cut, let alone inside a character.
  char buf[SPOOL_TYPE_NAME_MAX + 96];
  snprintf(buf, sizeof(buf), T(STR_SPT_CREATED), name ? name : "", spoolTypeGrams(grams));
  lv_label_set_text(s_note, buf);
  lv_obj_set_style_text_color(s_note, lv_color_hex(UI_COL_GOOD), 0);
  emptyShowWeight(true);
}

// The name entry was cancelled (its keyboard key): back to the weight,
// nothing sent.
static void emptyTypeCancelled() {
  s_naming = false;
  if (s_scr) lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_HIDDEN);
}

static void emptyPartCb(lv_event_t *e) {
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i < 0 || i >= SPOOL_EXTRAS_MAX) return;
  const uint8_t bit = (uint8_t)(1u << i);
  if (s_mask & bit) {
    s_mask &= (uint8_t)~bit;
  } else {
    int picked = 0;
    for (int k = 0; k < SPOOL_EXTRAS_MAX; k++) picked += (s_mask >> k) & 1;
    if (picked >= SPOOL_EXTRAS_PICK) return;   // as on a spool: untick one first
    s_mask |= bit;
  }
  emptyShowParts();
  emptyShowWeight(true);
}

static void emptyNextCb(lv_event_t *e) {
  (void)e;
  // The reading at the moment of the press, as the tare entry takes a
  // weighed empty spool.
  const float g = emptyBare();
  if (!scale_ready || !spoolTypeTarePlausible(g)) return;   // greyed, the press is ignored
  char names[96];
  spoolExtrasNames(s_mask, names, sizeof(names));
  logSDf("UI: empty spool weighed for a new type, %.1f g (reading %.1f g, parts: %s)",
         g, scale_weight_g, names[0] ? names : "none");
  showSpoolTypeName("", g, emptyTypeCreated, emptyTypeCancelled);
  if (!isSpoolTypeNameOpen()) return;
  // Hidden, not closed: a cancel brings it back as it was.
  s_naming = true;
  lv_obj_add_flag(s_scr, LV_OBJ_FLAG_HIDDEN);
}

void showEmptySpoolType() {
  if (!g_scale_fitted || !spoolTypesActive()) {
    logSD("UI: empty spool type not opened, no load cell or not Bambuddy in Spoolman mode");
    return;
  }
  logSD("SHOW: EmptySpoolType");
  closeEmptySpoolType();
  s_mask = spoolExtrasClamp(spoolExtrasDefault());

  s_scr = buildOverlayScreen();
  // Over the scale menu, which stays where it is: back closes this and the
  // menu is there again. The close button goes through showMainScreen(),
  // whose closeConfirmPopups() reaches closeTareEntry() and so this.
  buildSubHeader(s_scr, T(STR_SPT_EMPTY_TITLE), [](lv_event_t *e) {
    logSD("BTN: Back -> Scale");
    closeEmptySpoolType();
  });

  s_weight = lv_label_create(s_scr);
  lv_obj_set_style_text_font(s_weight, &lv_font_montserrat_ext_36, 0);
  lv_obj_align(s_weight, LV_ALIGN_TOP_MID, 0, 54);

  s_note = lv_label_create(s_scr);
  lv_label_set_long_mode(s_note, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_note, 440);
  lv_obj_set_style_text_align(s_note, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(s_note, UI_FONT_SMALL, 0);
  lv_obj_set_style_text_color(s_note, lv_color_hex(UI_COL_CAPTION), 0);
  lv_label_set_text(s_note, T(STR_SPT_EMPTY_HINT));
  lv_obj_align(s_note, LV_ALIGN_TOP_MID, 0, 100);

  // One switch per part set up, at most eight: two to a row up to four, four
  // to a row beyond, so they never take more than two rows.
  int used = 0;
  for (int i = 0; i < SPOOL_EXTRAS_MAX; i++) used += spoolExtraUsed(i) ? 1 : 0;
  const int EDGE = 10, GAP = 6, ITEM_H = 40, GRID_Y = 140, NEXT_H = 52;
  const int cols   = used > 4 ? 4 : 2;
  const int item_w = (480 - 2 * EDGE - (cols - 1) * GAP) / cols;
  int shown = 0;
  for (int i = 0; i < SPOOL_EXTRAS_MAX; i++) {
    s_part[i] = nullptr;
    if (!spoolExtraUsed(i)) continue;
    const int col = shown % cols, row = shown / cols;
    shown++;
    lv_obj_t *b = lv_btn_create(s_scr);
    s_part[i] = b;
    lv_obj_set_size(b, item_w, ITEM_H);
    lv_obj_set_pos(b, EDGE + col * (item_w + GAP), GRID_Y + row * (ITEM_H + GAP));
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_PRESS_FILL), LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, UI_RADIUS_BTN, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    char buf[40];
    snprintf(buf, sizeof(buf), "%s  %.1f g", spoolExtraAt(i).name, spoolExtraAt(i).grams);
    lv_label_set_text(l, buf);
    lv_obj_set_style_text_font(l, UI_FONT_SMALL, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, item_w - 12);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, emptyPartCb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }

  s_next = lv_btn_create(s_scr);
  lv_obj_set_size(s_next, 480 - 2 * EDGE, NEXT_H);
  lv_obj_set_pos(s_next, EDGE, 320 - EDGE - NEXT_H);
  lv_obj_set_style_bg_color(s_next, lv_color_hex(UI_COL_OK_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(s_next, UI_RADIUS_BTN, 0);
  lv_obj_set_style_shadow_width(s_next, 0, 0);
  char next_buf[32];
  snprintf(next_buf, sizeof(next_buf), "%s  " LV_SYMBOL_RIGHT, T(STR_BTN_NEXT));
  lv_obj_t *ln = lv_label_create(s_next);
  lv_label_set_text(ln, next_buf);
  lv_obj_set_style_text_font(ln, UI_FONT_BODY, 0);
  lv_obj_center(ln);
  lv_obj_add_event_cb(s_next, emptyNextCb, LV_EVENT_CLICKED, NULL);

  emptyShowParts();
  emptyShowWeight(true);
  lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_HIDDEN);
}

void emptySpoolTypeTick() {
  if (!s_scr) return;
  if (s_naming) {
    if (isSpoolTypeNameOpen()) return;   // hidden under it, nothing to draw
    // The name entry went without an answer (closed from elsewhere): this
    // screen must not stay hidden and open.
    s_naming = false;
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_HIDDEN);
  }
  emptyShowWeight(false);
}
