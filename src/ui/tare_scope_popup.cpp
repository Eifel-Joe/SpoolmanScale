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

// measured: the value is a reading off the pad, so a bag on it would be in
// there too. A typed value knows nothing of the bag.
static void showSpoolWeightPopup(float grams, bool then_new_spool, bool measured = true) {
  s_tare_prompt_g = grams;
  s_then_new = then_new_spool;
  // The typed value only gets here for the spool its entry was opened for,
  // see tare_entry.cpp.
  s_tare_spool_id = sm_id;

      // Sub-popup: where should the spool weight be written?
      const float w = s_tare_prompt_g;
      // Not every backend has all three places to put it. BamBuddy has no
      // filament and no vendor object, only the spool's own core weight,
      // which its Spoolman proxy stores too (#2908). Buttons for those
      // are not built rather than built and left to fail.
      const bool scope_spool  = backendCanTareSpool();
      const bool scope_shared = backendCanTareFilamentOrVendor();

      releaseScreen(&s_scope_popup);
      lv_obj_t *popup = lv_obj_create(lv_scr_act());
      s_scope_popup = popup;
      lv_obj_set_size(popup, 480, 320);
      lv_obj_set_pos(popup, 0, 0);
      lv_obj_set_style_bg_color(popup, lv_color_hex(UI_COL_GROUND), 0);
      lv_obj_set_style_bg_opa(popup, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(popup, 0, 0);
      lv_obj_set_style_pad_all(popup, 0, 0);
      lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

      // Title
      lv_obj_t *title = lv_label_create(popup);
      char title_buf[48];
      snprintf(title_buf, sizeof(title_buf), T(STR_SPOOL_WEIGHT_TITLE), w);
      lv_label_set_text(title, title_buf);
      lv_obj_set_style_text_color(title, lv_color_hex(UI_COL_ACCENT), 0);
      lv_obj_set_style_text_font(title, &lv_font_montserrat_ext_14, 0);
      lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

      // Rows are placed one after another so a missing scope leaves no hole.
      int row_y = 36;

      // Button 1: this spool
      lv_obj_t *b1 = lv_btn_create(popup);
      lv_obj_set_size(b1, 460, 60); lv_obj_set_pos(b1, 10, row_y);
      if (!scope_spool) lv_obj_add_flag(b1, LV_OBJ_FLAG_HIDDEN);
      else row_y += 70;
      lv_obj_set_style_bg_color(b1, lv_color_hex(UI_COL_CHIP), 0);
      lv_obj_set_style_radius(b1, 8, 0); lv_obj_set_style_shadow_width(b1, 0, 0);
      { lv_obj_t *l = lv_label_create(b1);
        lv_label_set_text(l, T(backendIsFilaMan() ? STR_BTN_THIS_SPOOL_FM
                                                  : STR_BTN_THIS_SPOOL));
        lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_INK_2), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_16, 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(l); }
      lv_obj_add_event_cb(b1, [](lv_event_t *e) {
        confirmParkTare(TARE_SCOPE_SPOOL, s_tare_prompt_g, s_tare_spool_id, s_then_new);
        releaseScreen(&s_scope_popup);
      }, LV_EVENT_CLICKED, NULL);

      // Button 2: this filament
      lv_obj_t *b2 = lv_btn_create(popup);
      lv_obj_set_size(b2, 460, 60); lv_obj_set_pos(b2, 10, row_y);
      if (!scope_shared) lv_obj_add_flag(b2, LV_OBJ_FLAG_HIDDEN);
      else row_y += 70;
      lv_obj_set_style_bg_color(b2, lv_color_hex(UI_COL_FILAMENT_BG), 0);
      lv_obj_set_style_radius(b2, 8, 0); lv_obj_set_style_shadow_width(b2, 0, 0);
      { lv_obj_t *l = lv_label_create(b2);
        lv_label_set_text(l, T(backendIsFilaMan() ? STR_BTN_THIS_FILAMENT_FM
                                                  : STR_BTN_THIS_FILAMENT));
        lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_INK_2), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_16, 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(l); }
      lv_obj_add_event_cb(b2, [](lv_event_t *e) {
        confirmParkTare(TARE_SCOPE_FILAMENT, s_tare_prompt_g, s_tare_spool_id, s_then_new);
        releaseScreen(&s_scope_popup);
      }, LV_EVENT_CLICKED, NULL);

      // Button 3: vendor
      lv_obj_t *b3 = lv_btn_create(popup);
      lv_obj_set_size(b3, 460, 60); lv_obj_set_pos(b3, 10, row_y);
      if (!scope_shared) lv_obj_add_flag(b3, LV_OBJ_FLAG_HIDDEN);
      else row_y += 70;
      lv_obj_set_style_bg_color(b3, lv_color_hex(UI_COL_AMBER_BG), 0);
      lv_obj_set_style_radius(b3, 8, 0); lv_obj_set_style_shadow_width(b3, 0, 0);
      { lv_obj_t *l = lv_label_create(b3);
        // Spoolman's vendor is FilaMan's manufacturer, and the bracketed field
        // names below the title only hold for Spoolman - see the lang.cpp entries.
        lv_label_set_text(l, T(backendIsFilaMan() ? STR_BTN_THIS_VENDOR_FM
                                                  : STR_BTN_THIS_VENDOR));
        lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_INK_2), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_16, 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(l); }
      lv_obj_add_event_cb(b3, [](lv_event_t *e) {
        confirmParkTare(TARE_SCOPE_VENDOR, s_tare_prompt_g, s_tare_spool_id, s_then_new);
        releaseScreen(&s_scope_popup);
      }, LV_EVENT_CLICKED, NULL);

      // Button 4: cancel, right under whatever was built above
      lv_obj_t *b4 = lv_btn_create(popup);
      lv_obj_set_size(b4, 460, 40); lv_obj_set_pos(b4, 10, row_y + 10);
      lv_obj_set_style_bg_color(b4, lv_color_hex(UI_COL_BAD_BG), 0);
      lv_obj_set_style_radius(b4, 8, 0); lv_obj_set_style_shadow_width(b4, 0, 0);
      { lv_obj_t *l = lv_label_create(b4);
        lv_label_set_text(l, T(STR_CANCEL));
        lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_BAD_TEXT), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_14, 0);
        lv_obj_center(l); }
      lv_obj_add_event_cb(b4, [](lv_event_t *e) {
        releaseScreen(&s_scope_popup);
      }, LV_EVENT_CLICKED, NULL);

      // A bag on the pad is counted into the reading, and this number may be
      // written to an entire brand. The device cannot tell whether one is
      // there, so it says so rather than silently subtracting a weight that
      // might not be on the scale at all.
      if (measured && bag_weight_g > 0.0f) {
        lv_obj_t *bag_hint = lv_label_create(popup);
        char bag_buf[64];
        snprintf(bag_buf, sizeof(bag_buf), T(STR_SPOOL_WEIGHT_BAG_HINT), bag_weight_g);
        lv_label_set_text(bag_hint, bag_buf);
        lv_obj_set_style_text_color(bag_hint, lv_color_hex(UI_COL_WARN), 0);
        lv_obj_set_style_text_font(bag_hint, &lv_font_montserrat_ext_12, 0);
        lv_obj_align(bag_hint, LV_ALIGN_TOP_MID, 0, 300);
      }

    }

void showSpoolWeightScope(float grams, bool measured, bool then_new_spool) {
  showSpoolWeightPopup(grams, then_new_spool, measured);
}
