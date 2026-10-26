#include "app/id_spool.h"

#include <Arduino.h>
#include <lvgl.h>
#include <math.h>

#include "app/app_state.h"
#include "app_config.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/auto_weight_state.h"
#include "services/location_state.h"
#include "services/user_options.h"
#include "ui/main_screen_helpers.h"
#include "ui/no_tag_create.h"   // noTagLabelAskOpen()
#include "ui/spoolman_lookup.h"
#include "ui/status_clear.h"
#include "ui/tag_display.h"
#include "ui/theme.h"
#include "ui/ui_common.h"
#include "ui/weight_button.h"

static bool          s_active     = false;
static int           s_id         = 0;
static bool          s_seen_load  = false;   // the spool has been on the pad since
static unsigned long s_since_ms   = 0;
static unsigned long s_lifted_ms  = 0;       // when it left the pad, 0 while it is on
// The most the pad carried while the spool lay there. Not the last reading:
// the moving average is already on its way down when the lift shows.
static float         s_load_g     = 0.0f;
static bool          s_lift_news  = false;   // lifted, not yet taken by the loop

bool idSpoolActive() { return s_active; }

static void paintStatus() {
  lv_label_set_text(lbl_nfc_dot, LV_SYMBOL_BULLET);
  lv_obj_set_style_text_color(lbl_nfc_dot, lv_color_hex(UI_COL_STATUS_BLUE), 0);
  lv_label_set_text(lbl_status, T(STR_ID_SPOOL_STATUS));
  lv_obj_set_style_text_color(lbl_status, lv_color_hex(UI_COL_STATUS_BLUE), 0);
}


static void endIdSpool(const char* why, bool clear_display) {
  logSDf("IdSpool: spool %d no longer shown (%s)", s_id, why);
  s_active = false;
  // Back to the idle look: the automatic one again, if it is on.
  weightButtonIdle();
  if (clear_display) clearTagDisplay();
}

void idSpoolAdopt(int spool_id) {
  // A tag put down while the id was being typed is the better answer.
  if (tag_present) {
    logSDf("IdSpool: spool %d not adopted, a tag is on the reader", spool_id);
    return;
  }
  if (s_active) endIdSpool("replaced", false);
  clearTagDisplay();
  querySpoolmanById(spool_id);
  // Failed reads leave sm_found false; querySpoolmanById() already said why.
  if (!sm_found || sm_id != spool_id) {
    logSDf("IdSpool: spool %d could not be loaded", spool_id);
    return;
  }
  s_active    = true;
  s_id        = spool_id;
  s_seen_load = scale_weight_g >= ID_SPOOL_LOAD_MIN_G;
  s_since_ms  = millis();
  s_lifted_ms = 0;
  s_load_g    = scale_weight_g;
  s_lift_news = false;
  // A spool called up by its id is a new trip, like a tag put down again:
  // what was answered about it last time does not hold back the questions.
  g_loc_popup_shown_for_id = -1;
  paintStatus();
  // Plain while the state holds: the automatic look would promise a save
  // that does not happen.
  weightButtonIdle();
  logSDf("IdSpool: spool %d shown without a tag (load %.0fg)", spool_id, scale_weight_g);
}

void idSpoolTick() {
  if (!s_active) return;
  if (tag_present) { endIdSpool("tag took over", false); return; }
  // Cleared or replaced by something else: a backend switch, an archive.
  if (!sm_found || sm_id != s_id) { endIdSpool("display changed", false); return; }
  // The clear button behind the status text, see statusClearSync() in the loop.
  if (statusClearTaken()) { endIdSpool("cleared by hand", true); return; }
  // Without a load cell nothing below can tell: the button and a tag end it.
  if (!g_scale_fitted) return;
  if (scale_weight_g >= ID_SPOOL_LOAD_MIN_G) {
    if (s_lifted_ms || !s_seen_load) s_load_g = 0.0f;   // a new stay on the pad
    s_seen_load = true;
    s_lifted_ms = 0;
    s_load_g    = fmaxf(s_load_g, scale_weight_g);
    s_lift_news = false;
    return;
  }
  if (!s_seen_load) {
    if (millis() - s_since_ms > ID_SPOOL_IDLE_MS) endIdSpool("never put on the pad", true);
    return;
  }
  // Lifted: shown a little longer, so a question about it, or the location
  // or AMS button tapped on the way, still finds it. Put back in time, it goes
  // on as before. Not over while anything covers the main screen: a location
  // picker writes to the spool on display, and has 30 s for it.
  // One more case is ours: while "Print a label?" stands for a spool just
  // created without a tag, the clock starts over every pass. The card covers
  // the screen, but the clock ran on behind it, and an answer later than the
  // 15 s ended the spool in the very pass the card closed in: the location
  // question found nothing to ask about and "Print" found no spool. Now the
  // 15 s count from the moment the label question is closed. Not for every
  // covering screen: that would change what the original does for them (see
  // test_no_tag_finish.py).
  if (!s_lifted_ms) {
    s_lifted_ms = millis();
    s_lift_news = true;
    logSDf("IdSpool: spool %d lifted, shown for %lu s more", s_id, ID_SPOOL_LIFTED_MS / 1000UL);
  } else if (noTagLabelAskOpen()) {
    s_lifted_ms = millis();
  } else if (millis() - s_lifted_ms > ID_SPOOL_LIFTED_MS && !uiMainScreenCovered()) {
    endIdSpool("taken off the pad", true);
  }
}

bool idSpoolTakeLift(float* load_g) {
  if (!s_active || !s_lift_news) return false;
  s_lift_news = false;
  if (load_g) *load_g = s_load_g;
  return true;
}
