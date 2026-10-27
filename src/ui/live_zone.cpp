#include "live_zone.h"
#include "app/app_state.h"
#include "hardware/scale_state.h"
#include "lang.h"
#include "theme.h"
#include "hardware/sd_logger.h"

#include <lvgl.h>

namespace {

// Holds the zone's labels so a changed bag weight can swap them. Created once
// where the labels used to be created one by one, so its place among the
// main screen's children - under every overlay - stays where theirs was. Not
// clickable and not drawn: a tap goes through it to whatever lies below.
lv_obj_t *s_zone = nullptr;

// The zone's top left corner on the screen. The label positions below stay
// in screen coordinates and are moved into the container by this much.
#define ZONE_X  210
#define ZONE_Y  185
#define ZONE_W  200
#define ZONE_H   80

lv_obj_t* caption(StringID id, lv_coord_t x, lv_coord_t y) {
  lv_obj_t *l = lv_label_create(s_zone);
  lv_label_set_text(l, T(id));
  lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_CAPTION), 0);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_12, 0);
  lv_obj_set_pos(l, x - ZONE_X, y - ZONE_Y);
  return l;
}

lv_obj_t* value(lv_coord_t x, lv_coord_t y, const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = lv_label_create(s_zone);
  lv_label_set_text(l, "");
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_pos(l, x - ZONE_X, y - ZONE_Y);
  return l;
}

// Four lines in 79 px: netto and its diff on the first, the total on the
// second, the netto without the bag and its own diff on the third.
void buildWithBag() {
  caption(STR_LBL_SCALE_SPOOL_CAP, 218, 189);
  lbl_scale_weight = value(218, 201, &lv_font_montserrat_ext_20, UI_COL_WARN);
  caption(STR_LBL_DIFF_CAP, 344, 189);
  lbl_raw_info = value(344, 205, &lv_font_montserrat_ext_16, UI_COL_CAPTION);
  caption(STR_LBL_TOTAL_CAP, 218, 225);
  lbl_spoolman_dried = value(286, 225, &lv_font_montserrat_ext_14, UI_COL_VALUE_BLUE);
  caption(STR_LBL_WO_BAG_CAP, 218, 243);
  lbl_keys = value(286, 243, &lv_font_montserrat_ext_14, UI_COL_WARN);
  lbl_bag_sm_diff = value(344, 241, &lv_font_montserrat_ext_16, UI_COL_CAPTION);
  lbl_spool_tare = nullptr;
}

// Without a bag the third line would repeat the first, so there are two,
// the room goes into the numbers, and the right column gets the spool
// weight the netto subtracts: total minus spool is what the big number
// shows, and here the whole sum is on the screen.
void buildBare() {
  caption(STR_LBL_SCALE_SPOOL_CAP, 218, 189);
  lbl_scale_weight = value(218, 203, &lv_font_montserrat_ext_24, UI_COL_WARN);
  caption(STR_LBL_DIFF_CAP, 344, 189);
  lbl_raw_info = value(344, 205, &lv_font_montserrat_ext_20, UI_COL_CAPTION);
  caption(STR_LBL_TOTAL_CAP, 218, 243);
  lbl_spoolman_dried = value(286, 241, &lv_font_montserrat_ext_16, UI_COL_VALUE_BLUE);
  caption(STR_LBL_SPOOL_TARE_CAP, 344, 229);
  lbl_spool_tare = value(344, 243, &lv_font_montserrat_ext_16, UI_COL_INK_2);
  lbl_keys = nullptr;
  lbl_bag_sm_diff = nullptr;
}

void fillLiveZone() {
  if (bagInUse()) buildWithBag();
  else            buildBare();
  lv_label_set_text(lbl_scale_weight, scale_ready ? "0 g" : "---");
}

}  // namespace

void buildLiveZone() {
  s_zone = lv_obj_create(lv_scr_act());
  lv_obj_remove_style_all(s_zone);
  lv_obj_set_pos(s_zone, ZONE_X, ZONE_Y);
  lv_obj_set_size(s_zone, ZONE_W, ZONE_H);
  // A long figure may reach past the box; it is drawn, not cut.
  lv_obj_add_flag(s_zone, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_clear_flag(s_zone, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(s_zone, LV_OBJ_FLAG_SCROLLABLE);
  fillLiveZone();
}

void rebuildLiveZone() {
  if (!s_zone) return;
  lv_obj_clean(s_zone);
  fillLiveZone();
  logSDf("Live zone rebuilt %s bag", bagInUse() ? "with" : "without");
}
