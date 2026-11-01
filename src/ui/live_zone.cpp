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

// The diffs' column: from DIFF_X to the end of column 2, 8 px before the
// divider to TARE at x = 408 (main_screen.cpp).
#define DIFF_X      344
#define DIFF_RIGHT  400

// The fonts a diff may step down through, largest first. The layout gives
// each diff one of them; liveZoneSetDiff() goes down from there.
const lv_font_t* const DIFF_STEPS[] = {
  &lv_font_montserrat_ext_20, &lv_font_montserrat_ext_16,
  &lv_font_montserrat_ext_14, &lv_font_montserrat_ext_12,
};

// The two diffs of the layout on the screen: the label, the font and the top
// the layout gave it. The second is empty without a bag.
struct DiffSlot {
  lv_obj_t*        lbl;
  const lv_font_t* font;
  lv_coord_t       y;
};
DiffSlot s_diffs[2] = {};

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

// A diff in its column, kept in slot i for liveZoneSetDiff().
lv_obj_t* diff(int i, lv_coord_t y, const lv_font_t *font) {
  lv_obj_t *l = value(DIFF_X, y, font, UI_COL_CAPTION);
  s_diffs[i] = {l, font, y};
  return l;
}

// The height from a label's top to the baseline of its text.
lv_coord_t ascent(const lv_font_t *font) {
  return lv_font_get_line_height(font) - font->base_line;
}

// Four lines in 79 px: netto and its diff on the first, the total on the
// second, the netto without the bag and its own diff on the third.
void buildWithBag() {
  caption(STR_LBL_SCALE_SPOOL_CAP, 218, 189);
  lbl_scale_weight = value(218, 201, &lv_font_montserrat_ext_20, UI_COL_WARN);
  caption(STR_LBL_DIFF_CAP, DIFF_X, 189);
  lbl_raw_info = diff(0, 205, &lv_font_montserrat_ext_16);
  caption(STR_LBL_TOTAL_CAP, 218, 225);
  lbl_spoolman_dried = value(286, 225, &lv_font_montserrat_ext_14, UI_COL_VALUE_BLUE);
  caption(STR_LBL_WO_BAG_CAP, 218, 243);
  lbl_keys = value(286, 243, &lv_font_montserrat_ext_14, UI_COL_WARN);
  lbl_bag_sm_diff = diff(1, 241, &lv_font_montserrat_ext_16);
  lbl_spool_tare = nullptr;
}

// Without a bag the third line would repeat the first, so there are two,
// the room goes into the numbers, and the right column gets the spool
// weight the netto subtracts: total minus spool is what the big number
// shows, and here the whole sum is on the screen.
void buildBare() {
  caption(STR_LBL_SCALE_SPOOL_CAP, 218, 189);
  lbl_scale_weight = value(218, 203, &lv_font_montserrat_ext_24, UI_COL_WARN);
  caption(STR_LBL_DIFF_CAP, DIFF_X, 189);
  lbl_raw_info = diff(0, 205, &lv_font_montserrat_ext_20);
  caption(STR_LBL_TOTAL_CAP, 218, 243);
  lbl_spoolman_dried = value(286, 241, &lv_font_montserrat_ext_16, UI_COL_VALUE_BLUE);
  caption(STR_LBL_SPOOL_TARE_CAP, 344, 229);
  lbl_spool_tare = value(344, 243, &lv_font_montserrat_ext_16, UI_COL_INK_2);
  lbl_keys = nullptr;
  lbl_bag_sm_diff = nullptr;
}

void fillLiveZone() {
  s_diffs[0] = s_diffs[1] = DiffSlot{};
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

// Wurzel (f03): a diff stood from x = 344 in the layout's font with no bound,
// and "-1038 g" in Montserrat 20 is about 72 px: it ran over the divider to
// TARE at 408. At the UI's own 480 x 320 already, on every board.
// Fix: the largest font of DIFF_STEPS, from the layout's own down, in which
// the text fits the column; 12 holds "-9999 g". The top moves by the
// difference of the ascents, so the number keeps the row's baseline.
// NOT-TO-DO: LONG_DOT on a fixed width: it would cut digits off a number.
// Dropping the blank before "g": it saves 4 px at 20, far from the 16 short.
// See test_live_zone_diff.py.
void liveZoneSetDiff(lv_obj_t* lbl, const char* text) {
  if (!lbl || !text) return;
  const DiffSlot* slot = nullptr;
  for (const DiffSlot& d : s_diffs) {
    if (d.lbl == lbl) slot = &d;
  }
  if (!slot) return;
  const int steps = sizeof(DIFF_STEPS) / sizeof(DIFF_STEPS[0]);
  int i = 0;
  while (i < steps - 1 && DIFF_STEPS[i] != slot->font) i++;
  const lv_font_t* font = DIFF_STEPS[i];
  for (; i < steps; i++) {
    font = DIFF_STEPS[i];
    lv_point_t size;
    lv_txt_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (size.x <= DIFF_RIGHT - DIFF_X) break;
  }
  lv_label_set_text(lbl, text);
  if (lv_obj_get_style_text_font(lbl, LV_PART_MAIN) == font) return;
  lv_obj_set_style_text_font(lbl, font, 0);
  lv_obj_set_y(lbl, slot->y + ascent(slot->font) - ascent(font) - ZONE_Y);
}
