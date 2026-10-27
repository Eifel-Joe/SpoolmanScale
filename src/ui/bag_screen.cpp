#include "bag_screen.h"
#include "navigation.h"
#include "app/app_state.h"
#include "app/deferred_actions.h"

#include <Arduino.h>
#include <lvgl.h>
#include <cmath>
#include <cstdlib>

#include "hardware/scale_state.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "num_pad.h"
#include "scale_menu.h"

// Whole grams up to 999: every row that shows the bag rounds to them.
#define BAG_INPUT_MAX_CHARS  3
#define BAG_MAX_G         1000.0f

static void backToScaleMenu() {
  hideAllOverlays();
  if (scr_scale_sub) { lv_obj_del(scr_scale_sub); scr_scale_sub = nullptr; }
  buildScaleSubScreen();
  lv_obj_clear_flag(scr_scale_sub, LV_OBJ_FLAG_HIDDEN);
}

static bool onBagDone(const char *text) {
  // Whole grams: every row that shows the bag rounds to them, and
  // bagInUse() must not say yes to a value those rows call 0 g. An empty
  // field is 0, no bag.
  const float w = roundf(atof(text));
  if (w < 0.0f || w >= BAG_MAX_G) {
    numPadSetHint(T(STR_BAG_INVALID), true);
    return false;
  }
  // The main screen's weight zone has a layout with the bag lines and one
  // without, so a value that crosses 0 swaps it. A value that stays on its
  // side of 0 is live at once.
  const bool had_bag = bagInUse();
  saveBagWeight(w);
  if (had_bag != bagInUse()) live_zone_rebuild_pending = true;
  backToScaleMenu();
  return true;
}

void showBagPad() {
  char initial[12] = "";
  if (bag_weight_g > 0.0f) snprintf(initial, sizeof(initial), "%.0f", bag_weight_g);
  NumPadSpec spec = {};
  spec.title      = T(STR_BTN_BAGWEIGHT);
  spec.hint       = T(STR_BAG_DESC);
  spec.unit       = "g";
  spec.primary    = T(STR_BTN_SAVE);
  spec.initial    = initial;
  spec.max_chars  = BAG_INPUT_MAX_CHARS;
  spec.decimal    = false;
  spec.help_title = STR_BTN_BAGWEIGHT;
  spec.help_text  = STR_BAG_HELP_TEXT;
  spec.on_done    = onBagDone;
  spec.on_leave   = backToScaleMenu;
  hideAllOverlays();
  showNumPad(spec);
}
