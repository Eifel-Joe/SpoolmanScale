#include "ui/sort_entry_internal.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdint.h>
#include <string.h>

#include "hardware/sd_logger.h"
#include "services/text_util.h"
#include "ui/sort_entry_screen.h"
#include "ui/theme.h"
#include "ui/ui_common.h"
// Last: lang.h defines T(), which a template parameter of a header above
// would trip over.
#include "lang.h"

// ============================================================
//  THE KEYBOARD OF THE ENTRY
//
//  An own copy of "New spool type" (spool_type_name.cpp): a title, one line
//  to type in, a line for a word under it and LVGL's keyboard at the bottom.
//  Like that one it is a full screen on lv_scr_act(), over the step, and not
//  a child of the entry's screen: a close of the entry in the same pass
//  would hand both to lv_obj_del_async(), and the parent's delete would
//  free the child before the child's own delete ran.
//
//  OK only parks the text; sortKeysTick() hands it to the step on the loop,
//  where the step may ask the inventory, and closes the keys afterwards
//  unless the step refused the text with a word (sortKeysSay()).
// ============================================================

// The longest field that is typed: a colour name, SORT_NAME_MAX - 1 bytes.
#define SORT_KEYS_TEXT_MAX  SORT_NAME_MAX
// Title, field and word line keep this far from the sides; the field as wide
// as the one of "New spool type".
#define SORT_KEYS_SIDE      10
#define SORT_KEYS_FIELD_W   440
#define SORT_KEYS_FIELD_H   44
#define SORT_KEYS_KB_H      160

static lv_obj_t* s_keys = nullptr;
static lv_obj_t* s_ta   = nullptr;
static lv_obj_t* s_say  = nullptr;
static size_t    s_max  = SORT_KEYS_TEXT_MAX - 1;
static void    (*s_done)(const char* text) = nullptr;
// The text the check mark parked, for sortKeysTick().
static bool      s_parked = false;
static char      s_text[SORT_KEYS_TEXT_MAX] = "";
// How many keyboards were shown, and whether a word was said: after done the
// tick closes the keys only when done did neither.
static uint32_t  s_shown = 0;
static bool      s_said  = false;

void sortKeysClose() {
  // Deleted on the next timer pass: the keyboard's cancel key lands here
  // from inside its own event.
  releaseScreen(&s_keys);
  s_ta     = nullptr;
  s_say    = nullptr;
  s_done   = nullptr;
  s_parked = false;
}

void sortKeysSay(const char* text) {
  s_said = true;
  if (s_say) lv_label_set_text(s_say, text ? text : "");
}

// The text as it goes on: without the blanks around it, at most s_max bytes
// and cut at a character boundary. lv_textarea counts characters, the fields
// of the entry count bytes.
static void keysText(const char* in, char* out, size_t out_size) {
  if (!in) in = "";
  while (*in == ' ') in++;
  utf8Cut(in, s_max, out, out_size);
  size_t n = strlen(out);
  while (n > 0 && out[n - 1] == ' ') out[--n] = '\0';
}

// The check mark reports READY on the keyboard and then on the field, the
// return key on the field alone, so this sits on both and can run twice for
// one press; the second run finds the text parked and returns.
static void onKeysReady(lv_event_t* e) {
  (void)e;
  if (!s_ta || s_parked) return;
  keysText(lv_textarea_get_text(s_ta), s_text, sizeof(s_text));
  s_parked = true;
}

// LVGL's keyboard key: only the keys go, the step under them stays.
static void onKeysCancel(lv_event_t* e) {
  (void)e;
  logSD("BTN: Enter filament, keys cancelled");
  sortKeysClose();
}

void sortKeysShow(uint8_t kind, int title_id, size_t max_len, void (*done)(const char* text)) {
  sortKeysClose();
  s_shown++;
  s_max  = (max_len > 0 && max_len < sizeof(s_text)) ? max_len : sizeof(s_text) - 1;
  s_done = done;
  logSDf("SHOW: SortEntry keys (kind %d, %u bytes)", (int)kind, (unsigned)s_max);

  lv_obj_t* scr = lv_obj_create(lv_scr_act());
  s_keys = scr;
  lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
  lv_obj_set_pos(scr, 0, 0);
  lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COL_GROUND), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_radius(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(scr);
  lv_label_set_text(title, (title_id >= 0 && title_id < STR_COUNT) ? T(title_id) : "");
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(title, LV_HOR_RES - 2 * SORT_KEYS_SIDE);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_text_font(title, UI_FONT_TITLE, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

  // One line. The limit is set before the text, which LVGL then adds one
  // character at a time; it counts characters, keysText() cuts the bytes.
  s_ta = lv_textarea_create(scr);
  lv_textarea_set_one_line(s_ta, true);
  lv_textarea_set_max_length(s_ta, (uint32_t)s_max);
  if (kind == SK_GRAMS)    lv_textarea_set_accepted_chars(s_ta, "0123456789");
  else if (kind == SK_HEX) lv_textarea_set_accepted_chars(s_ta, "#0123456789ABCDEFabcdef");
  lv_textarea_set_text(s_ta, "");
  lv_obj_set_size(s_ta, SORT_KEYS_FIELD_W, SORT_KEYS_FIELD_H);
  lv_obj_align(s_ta, LV_ALIGN_TOP_MID, 0, 44);
  lv_obj_set_style_text_font(s_ta, UI_FONT_BODY, 0);
  lv_obj_set_style_text_color(s_ta, lv_color_hex(UI_COL_INK_MAX), 0);
  lv_obj_set_style_bg_color(s_ta, lv_color_hex(UI_COL_ROW_PRESSED), 0);
  lv_obj_set_style_border_color(s_ta, lv_color_hex(UI_COL_POPUP_BORDER), 0);
  // The cursor shows where typing goes.
  lv_obj_add_state(s_ta, LV_STATE_FOCUSED);

  // Empty until the step refuses a text; two lines end above the keyboard.
  s_say = lv_label_create(scr);
  lv_label_set_text(s_say, "");
  lv_label_set_long_mode(s_say, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_say, SORT_KEYS_FIELD_W);
  lv_obj_align(s_say, LV_ALIGN_TOP_MID, 0, 98);
  lv_obj_set_style_text_font(s_say, UI_FONT_SMALL, 0);
  lv_obj_set_style_text_align(s_say, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(s_say, lv_color_hex(UI_COL_WARN), 0);

  // LVGL's own layout, QWERTY without umlauts; its keyboard key reports
  // CANCEL, the way back to the step.
  lv_obj_t* kb = lv_keyboard_create(scr);
  lv_keyboard_set_textarea(kb, s_ta);
  if (kind == SK_GRAMS) lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
  lv_obj_set_size(kb, LV_HOR_RES, SORT_KEYS_KB_H);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(kb, lv_color_hex(UI_COL_EMPTY), 0);
  lv_obj_set_style_border_width(kb, 0, 0);
  lv_obj_add_event_cb(kb, onKeysReady, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(s_ta, onKeysReady, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(kb, onKeysCancel, LV_EVENT_CANCEL, NULL);
}

void sortKeysTick() {
  if (!s_keys) return;
  // The entry's screen hidden (hideSpoolFlowOverlays()) or gone: the keys
  // go with it before the entry is closed on a later pass.
  lv_obj_t* under = sortEntryScreen();
  if (!under || lv_obj_has_flag(under, LV_OBJ_FLAG_HIDDEN)) {
    sortKeysClose();
    return;
  }
  if (!s_parked) return;
  s_parked = false;
  // Nothing, or only blanks and hyphens, which the rules take for no name.
  if (!s_text[0] || !sortEntrySame(s_text, s_text)) {
    sortKeysSay(T(STR_SORT_EMPTY));
    return;
  }
  // Copied out: done may open the keys anew.
  char text[sizeof(s_text)];
  memcpy(text, s_text, sizeof(text));
  void (*done)(const char*) = s_done;
  const uint32_t shown = s_shown;
  s_said = false;
  logSDf("SortEntry: typed \"%s\"", text);
  if (done) done(text);
  if (s_shown == shown && !s_said) sortKeysClose();
}
