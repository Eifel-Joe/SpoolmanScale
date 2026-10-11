#include "live_zone.h"
#include "app/app_state.h"
#include "hardware/scale_state.h"
#include "lang.h"
#include "theme.h"

#include <lvgl.h>

namespace {

lv_obj_t* caption(StringID id, lv_coord_t x, lv_coord_t y) {
  lv_obj_t *l = lv_label_create(lv_scr_act());
  lv_label_set_text(l, T(id));
  lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_CAPTION), 0);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_12, 0);
  lv_obj_set_pos(l, x, y);
  return l;
}

lv_obj_t* value(lv_coord_t x, lv_coord_t y, const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = lv_label_create(lv_scr_act());
  lv_label_set_text(l, "");
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_pos(l, x, y);
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

}  // namespace

void buildLiveZone() {
  if (bagInUse()) buildWithBag();
  else            buildBare();
  lv_label_set_text(lbl_scale_weight, scale_ready ? "0 g" : "---");
}
