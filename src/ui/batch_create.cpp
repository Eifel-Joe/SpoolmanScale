#include "ui/batch_create.h"

#include <stdint.h>
#include <stdio.h>

#include "hardware/sd_logger.h"
#include "services/batch_create_rules.h"
#include "ui/no_tag_create.h"
#include "ui/price_pad.h"
#include "ui/theme.h"
// Last, see the note on ArduinoJson and T() in tag_create_popup.cpp.
#include "lang.h"

// The count button: BATCH_CHIP_EDGE from the card's top and right edge, above
// the title (UI_CARD_TITLE_Y) and clear of the icon in the middle. Its text
// gets BATCH_CHIP_W - 8 (ui_budgets.tsv, the house button rule), which holds
// the widest of the three languages with the %d as the width gate measures it.
#define BATCH_CHIP_W     100
#define BATCH_CHIP_H      36
#define BATCH_CHIP_EDGE    8

static int       s_count      = BATCH_COUNT_MIN;
// The label of the button on the card that stands, nullptr while there is none,
// and what that card does with a new count.
static lv_obj_t* s_chip_lbl   = nullptr;
static void    (*s_changed)() = nullptr;

int batchCount() { return s_count; }

void batchCountReset() { s_count = BATCH_COUNT_MIN; }

const char* batchCreateCaption(char* buf, size_t cap) {
  if (s_count > BATCH_COUNT_MIN) snprintf(buf, cap, T(STR_BATCH_CREATE_N), s_count);
  else                           snprintf(buf, cap, "%s", T(STR_COPY_CARD_CREATE));
  return buf;
}

// The button's text, in its colour.
static void chipText(const char* text, uint32_t color) {
  if (!s_chip_lbl) return;
  lv_label_set_text(s_chip_lbl, text);
  lv_obj_set_style_text_color(s_chip_lbl, lv_color_hex(color), 0);
}

static void chipShowCount() {
  char buf[32];
  snprintf(buf, sizeof(buf), T(STR_BATCH_COUNT_CHIP), s_count);
  chipText(buf, UI_COL_INK_2);
}

// Wurzel: the pad answers from the loop (pricePadTick()), a pass or more after
// the tap, and the card may have been closed in between. A card that is closed
// is hidden at once and deleted on a later pass (releaseScreen(),
// hideSpoolFlowOverlays()): the label is still there, and the copy card would
// be built again for a create nobody sees.
// Fix: the answer counts only for a button whose card is still shown, with no
// object from its label up to the screen hidden. Once deleted, the button has
// forgotten its label (the LV_EVENT_DELETE in batchCountChip()).
// NOT-TO-DO: asking the card's own pointer, as copyFillTyped() asks
// scr_copy_confirm: hideSpoolFlowOverlays() hides the card and leaves the
// pointer set until the loop deletes it.
static bool chipStanding() {
  if (!s_chip_lbl) return false;
  for (lv_obj_t* o = s_chip_lbl; o; o = lv_obj_get_parent(o))
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return false;
  return true;
}

// The pad's answer, from the loop (pricePadTick()); a cancelled pad does not
// call. An emptied field types nothing, as for the fill weight: the count
// stays as it is. The count is a whole number in range (batchCountValid());
// anything else typed takes nothing and puts the range on the button, where
// the card has room for it, and the card is not told. A valid count is taken,
// shown on the button, and the card shows it on its create button.
static void countTyped(float typed) {
  if (!noTagMode() || !chipStanding()) return;
  if (typed <= 0.0f) return;
  if (!batchCountValid(typed)) {
    logSDf("Batch: count %.2f is out of range, not taken", typed);
    chipText(T(STR_BATCH_COUNT_RANGE), UI_COL_WARN);
    return;
  }
  s_count = (int)typed;
  logSDf("Batch: %d spools to create", s_count);
  chipShowCount();
  if (s_changed) s_changed();
}

void batchCountChip(lv_obj_t* box, void (*changed)()) {
  s_changed = changed;
  lv_obj_t* btn = lv_btn_create(box);
  lv_obj_set_size(btn, BATCH_CHIP_W, BATCH_CHIP_H);
  lv_obj_set_pos(btn, UI_POPUP_W - BATCH_CHIP_EDGE - BATCH_CHIP_W, BATCH_CHIP_EDGE);
  // In the look of the price button on the same cards.
  lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COL_ROW), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COL_ROW_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(UI_COL_LINE), 0);
  lv_obj_set_style_radius(btn, UI_RADIUS_BTN, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  // Wurzel: the price pad fills its field in with "%.2f", and then refuses
  // another digit: opened with the count, it shows "1.00", and a 3 takes four
  // presses of the delete key first.
  // Fix: the pad opens empty, as for a fill weight nobody knows; the count
  // stays on the button, and an emptied field keeps it (countTyped()).
  // NOT-TO-DO: the house number pad (num_pad.h), which would show the count and
  // replace it with the first digit: it answers inside its button's event,
  // where num_pad.h allows nothing that deletes a screen, and a new count
  // builds the copy card again; its close button also leaves the create for
  // the main screen.
  // See test_batch_flow.py (the card).
  lv_obj_add_event_cb(btn, [](lv_event_t*) {
    logSD("BTN: Card -> Count");
    showPricePadInfo(0, STR_BATCH_COUNT_TITLE, STR_BATCH_COUNT_INFO, countTyped);
  }, LV_EVENT_CLICKED, NULL);
  s_chip_lbl = lv_label_create(btn);
  lv_obj_set_style_text_font(s_chip_lbl, UI_FONT_SMALL, 0);
  lv_obj_center(s_chip_lbl);
  // A card built again (the copy card, for a new count) has its new button
  // before the old one is deleted, a pass later: only the label of the button
  // being deleted is forgotten.
  lv_obj_add_event_cb(btn, [](lv_event_t* e) {
    if (lv_event_get_user_data(e) == s_chip_lbl) {
      s_chip_lbl = nullptr;
      s_changed  = nullptr;
    }
  }, LV_EVENT_DELETE, s_chip_lbl);
  chipShowCount();
}
