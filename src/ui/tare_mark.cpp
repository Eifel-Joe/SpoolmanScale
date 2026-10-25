#include "tare_mark.h"
#include "services/auto_weight_state.h"
#include "lang.h"
#include "theme.h"

// Where the "A" sits, measured from the button's outer top right corner.
static const lv_coord_t TARE_MARK_INSET_X = 5;
static const lv_coord_t TARE_MARK_INSET_Y = 3;

static lv_obj_t *lbl_tare_auto = nullptr;

void tareMarkCreate(lv_obj_t* btn_tare) {
  if (!btn_tare) return;
  // Green is what the menu row uses for "ON". Aligned to the outer corner: on
  // its own LV_ALIGN_TOP_RIGHT lands inside the button's padding.
  lbl_tare_auto = lv_label_create(btn_tare);
  lv_label_set_text(lbl_tare_auto, T(STR_TARE_AUTO_MARK));
  lv_obj_set_style_text_color(lbl_tare_auto, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_text_font(lbl_tare_auto, &lv_font_montserrat_ext_12, 0);
  lv_obj_align(lbl_tare_auto, LV_ALIGN_TOP_RIGHT,
               lv_obj_get_style_pad_right(btn_tare, 0) - TARE_MARK_INSET_X,
               TARE_MARK_INSET_Y - lv_obj_get_style_pad_top(btn_tare, 0));
  tareMarkShow(g_auto_tare);
}

void tareMarkShow(bool on) {
  if (!lbl_tare_auto) return;
  if (on) lv_obj_clear_flag(lbl_tare_auto, LV_OBJ_FLAG_HIDDEN);
  else    lv_obj_add_flag(lbl_tare_auto, LV_OBJ_FLAG_HIDDEN);
}
