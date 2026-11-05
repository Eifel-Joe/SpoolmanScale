#include "busy_badge.h"

#include <lvgl.h>

#include "theme.h"

static lv_obj_t* s_badge = nullptr;

void busyBadgeShow() {
  if (s_badge || !lv_disp_get_default()) return;
  // On the top layer: above every screen and popup, and gone with one delete.
  s_badge = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_badge, 36, 36);
  lv_obj_align(s_badge, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
  lv_obj_set_style_radius(s_badge, 18, 0);
  lv_obj_set_style_bg_color(s_badge, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_bg_opa(s_badge, LV_OPA_90, 0);
  lv_obj_set_style_border_color(s_badge, lv_color_hex(UI_COL_LINE), 0);
  lv_obj_set_style_border_width(s_badge, 1, 0);
  lv_obj_set_style_shadow_width(s_badge, 0, 0);
  lv_obj_set_style_pad_all(s_badge, 0, 0);
  // Never in the way of a tap: the screen under it stays usable the moment
  // the call returns.
  lv_obj_clear_flag(s_badge, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(s_badge, LV_OBJ_FLAG_SCROLLABLE);
  // A loading ring. It stands still while the loop is stopped in the call, the
  // way a frozen frame does, and turns on every pass the loop gets between
  // two calls.
  lv_obj_t* ring = lv_spinner_create(s_badge, 1000, 70);
  lv_obj_set_size(ring, 20, 20);
  lv_obj_center(ring);
  lv_obj_set_style_arc_width(ring, 3, LV_PART_MAIN);
  lv_obj_set_style_arc_width(ring, 3, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(ring, lv_color_hex(UI_COL_LINE), LV_PART_MAIN);
  lv_obj_set_style_arc_color(ring, lv_color_hex(UI_COL_ACCENT), LV_PART_INDICATOR);
  lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
  // Drawn now: the loop is about to stop for as long as the call takes.
  lv_refr_now(NULL);
}

void busyBadgeHide() {
  if (!s_badge) return;
  lv_obj_del(s_badge);
  s_badge = nullptr;
}
