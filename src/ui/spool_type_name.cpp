#include "spool_type_name.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "hardware/sd_logger.h"
#include "loading_overlay.h"
#include "services/server_reach.h"
#include "services/spool_type_rules.h"
#include "services/spool_types.h"
#include "theme.h"
#include "ui_common.h"
#include "lang.h"   // T(), last: its macro T(id) is a name other headers may use

// The screen and the two widgets the loop writes to. The keyboard is not
// kept: nothing reaches it after it is built.
static lv_obj_t *s_scr    = nullptr;
static lv_obj_t *s_ta     = nullptr;
static lv_obj_t *s_status = nullptr;

static float            s_grams      = 0.0f;   // as handed in; whole grams when sent
static SpoolTypeCreated s_on_created = nullptr;
static void           (*s_on_cancel)() = nullptr;

// The name the check mark parked, trimmed, for spoolTypeNameTick().
static bool s_parked = false;
static char s_name[SPOOL_TYPE_NAME_MAX] = "";

bool isSpoolTypeNameOpen() {
  return s_scr != nullptr;
}

void closeSpoolTypeName() {
  // Deleted on the next timer pass: the keyboard's cancel key lands here
  // from inside its own event.
  releaseScreen(&s_scr);
  s_ta         = nullptr;
  s_status     = nullptr;
  s_on_created = nullptr;
  s_on_cancel  = nullptr;
  s_parked     = false;
}

static void nameStatus(const char *text, uint32_t col) {
  if (!s_status) return;
  lv_label_set_text(s_status, text);
  lv_obj_set_style_text_color(s_status, lv_color_hex(col), 0);
}

// The check mark reports READY on the keyboard and then on the text field,
// the return key on the field alone, so this sits on both and can run twice
// for one press; the second run finds the name parked and returns.
static void nameReadyCb(lv_event_t *e) {
  (void)e;
  if (!s_ta || s_parked) return;
  char name[SPOOL_TYPE_NAME_MAX];
  spoolTypeTrimName(name, sizeof(name), lv_textarea_get_text(s_ta));
  if (!name[0]) return;      // nothing but blanks: nothing to send
  // Copied now, sent from the loop: the requests block, and they do not
  // belong in the keyboard's callback.
  snprintf(s_name, sizeof(s_name), "%s", name);
  s_parked = true;
}

// LVGL's keyboard key. The callback is taken before the close drops it.
static void nameCancelCb(lv_event_t *e) {
  (void)e;
  logSD("UI: new spool type cancelled");
  void (*cb)() = s_on_cancel;
  closeSpoolTypeName();
  if (cb) cb();
}

void showSpoolTypeName(const char *prefill, float grams, SpoolTypeCreated on_created,
                       void (*on_cancel)()) {
  const bool active = spoolTypesActive();
  if (!active || !spoolTypeTarePlausible(grams)) {
    logSDf("UI: new spool type not opened, %s (%.1f g)",
           active ? "no empty spool weight" : "not Bambuddy in Spoolman mode", grams);
    if (on_cancel) on_cancel();
    return;
  }
  logSD("SHOW: SpoolTypeName");
  closeSpoolTypeName();
  s_grams      = grams;
  s_on_created = on_created;
  s_on_cancel  = on_cancel;

  lv_obj_t *scr = lv_obj_create(lv_scr_act());
  s_scr = scr;
  lv_obj_set_size(scr, 480, 320);
  lv_obj_set_pos(scr, 0, 0);
  lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COL_GROUND), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_radius(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // The weight that will be stored, in the whole grams it is stored in.
  char title[64];
  snprintf(title, sizeof(title), T(STR_SPT_NAME_TITLE), spoolTypeGrams(grams));
  lv_obj_t *lbl_title = lv_label_create(scr);
  lv_label_set_text(lbl_title, title);
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_text_font(lbl_title, UI_FONT_TITLE, 0);
  lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 12);

  // One line, as long as a catalog name may be. The limit counts characters
  // and the catalog bytes; spoolTypeTrimName() cuts a name that is longer in
  // bytes on a character boundary. The limit is set before the text, which
  // LVGL then adds one character at a time.
  s_ta = lv_textarea_create(scr);
  lv_textarea_set_one_line(s_ta, true);
  lv_textarea_set_max_length(s_ta, SPOOL_TYPE_NAME_MAX - 1);
  lv_textarea_set_text(s_ta, prefill ? prefill : "");
  lv_obj_set_size(s_ta, 440, 44);
  lv_obj_align(s_ta, LV_ALIGN_TOP_MID, 0, 44);
  lv_obj_set_style_text_font(s_ta, UI_FONT_BODY, 0);
  lv_obj_set_style_text_color(s_ta, lv_color_hex(UI_COL_INK_MAX), 0);
  lv_obj_set_style_bg_color(s_ta, lv_color_hex(UI_COL_ROW_PRESSED), 0);
  lv_obj_set_style_border_color(s_ta, lv_color_hex(UI_COL_POPUP_BORDER), 0);
  // The cursor shows at the end of what was filled in, where typing goes on.
  lv_obj_add_state(s_ta, LV_STATE_FOCUSED);

  // Empty until the loop has an answer: a name that is taken, or a failure.
  s_status = lv_label_create(scr);
  lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_status, 440);
  lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 98);
  lv_obj_set_style_text_font(s_status, UI_FONT_SMALL, 0);
  lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
  nameStatus("", UI_COL_CAPTION);

  // LVGL's own layout (wifi_setup_screen.cpp keeps a copy without the
  // keyboard key; a name needs none of the extra signs the password page has).
  // Its keyboard key reports CANCEL, which is the way out of this screen.
  lv_obj_t *kb = lv_keyboard_create(scr);
  lv_keyboard_set_textarea(kb, s_ta);
  lv_obj_set_size(kb, 480, 160);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(kb, lv_color_hex(UI_COL_EMPTY), 0);
  lv_obj_set_style_border_width(kb, 0, 0);
  lv_obj_add_event_cb(kb, nameReadyCb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(s_ta, nameReadyCb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(kb, nameCancelCb, LV_EVENT_CANCEL, NULL);
}

void spoolTypeNameTick() {
  if (!s_scr || !s_parked) return;
  s_parked = false;
  const int grams = spoolTypeGrams(s_grams);
  char line[96];

  // The touch panel is dead while the requests run; the overlay paints
  // itself, so the press is seen to have done something.
  loadingOverlayShow(T(STR_AMSD_SAVING));
  // Against the list as the server has it now, not as it was when a screen
  // opened (design note L15). A failed load is no list: a stale one could
  // pass a name that was added meanwhile, so nothing is sent.
  const int load = serverReachNote(spoolTypesLoad(), true);
  if (load != 200) {
    loadingOverlayHide();
    logSDf("UI: new spool type '%s' %d g not sent, catalog not loaded (HTTP %d)",
           s_name, grams, load);
    snprintf(line, sizeof(line), T(STR_SPT_NAME_FAIL), load);
    nameStatus(line, UI_COL_BAD);
    return;
  }
  if (spoolTypeNameTaken(spoolTypesList(), spoolTypesCount(), s_name)) {
    loadingOverlayHide();
    logSDf("UI: new spool type '%s' refused, the catalog has that name", s_name);
    nameStatus(T(STR_SPT_NAME_TAKEN), UI_COL_WARN);
    return;
  }
  // spoolTypesCreate() logs the result itself.
  const int code = serverReachNote(spoolTypesCreate(s_name, grams), true);
  loadingOverlayHide();
  if (code != 200) {
    snprintf(line, sizeof(line), T(STR_SPT_NAME_FAIL), code);
    nameStatus(line, UI_COL_BAD);
    return;
  }
  // Closed before the callback, which shows the screen this was opened from;
  // the name is copied out first, the close does not clear it but the next
  // entry would.
  char name[SPOOL_TYPE_NAME_MAX];
  snprintf(name, sizeof(name), "%s", s_name);
  SpoolTypeCreated cb = s_on_created;
  closeSpoolTypeName();
  if (cb) cb((float)grams, name);
}
