#include "ui/key_maps.h"

#include <lvgl.h>
#include <stdint.h>
#include <string.h>

#include "hardware/sd_logger.h"
#include "services/key_layout.h"
// Last: lang.h defines T(), which a template parameter of a header above
// would trip over.
#include "lang.h"

// ============================================================
//  THE KEYS OF A TEXT KEYBOARD: ON THE PANEL (w02, V43)
//
//  services/key_layout.cpp holds the pages as plain tables. Here they become
//  what lv_keyboard_set_map() takes, and the French accent key gets the
//  handler that opens its page.
// ============================================================

// lv_keyboard_def_event_cb() tells the control keys by their text
// (strcmp() against LV_SYMBOL_*), so a key that differed by one byte would be
// typed into the field instead of deleting or confirming. key_layout.h spells
// the symbols without LVGL; each is held to the build's own here.
static constexpr bool sameSymbol(const char* a, const char* b) {
  return *a == *b && (*a == '\0' || sameSymbol(a + 1, b + 1));
}
static_assert(sameSymbol(KEY_SYM_BACKSPACE, LV_SYMBOL_BACKSPACE), "KEY_SYM_BACKSPACE is not LVGL's");
static_assert(sameSymbol(KEY_SYM_NEW_LINE, LV_SYMBOL_NEW_LINE), "KEY_SYM_NEW_LINE is not LVGL's");
static_assert(sameSymbol(KEY_SYM_KEYBOARD, LV_SYMBOL_KEYBOARD), "KEY_SYM_KEYBOARD is not LVGL's");
static_assert(sameSymbol(KEY_SYM_LEFT, LV_SYMBOL_LEFT), "KEY_SYM_LEFT is not LVGL's");
static_assert(sameSymbol(KEY_SYM_RIGHT, LV_SYMBOL_RIGHT), "KEY_SYM_RIGHT is not LVGL's");
static_assert(sameSymbol(KEY_SYM_OK, LV_SYMBOL_OK), "KEY_SYM_OK is not LVGL's");

#define KEY_LANGS  (KL_FR + 1)
#define KEY_PAGES  (KP_ACCENTS + 1)

// The mode LVGL keeps each page under, by KeyPage.
static const lv_keyboard_mode_t KEY_MODE[KEY_PAGES] = {
  LV_KEYBOARD_MODE_TEXT_LOWER,   // KP_LOWER
  LV_KEYBOARD_MODE_TEXT_UPPER,   // KP_UPPER
  LV_KEYBOARD_MODE_SPECIAL,      // KP_SPECIAL
  LV_KEYBOARD_MODE_USER_1,       // KP_ACCENTS
};

// The control words of every layout: language, text or WiFi, page. Built the
// first time a page is set and kept, since LVGL reads them by pointer
// whenever that page is shown. 3 x 2 x 4 x 48 words, 2304 bytes.
static lv_btnmatrix_ctrl_t s_ctrl[KEY_LANGS][2][KEY_PAGES][KEY_LAYOUT_MAX_KEYS];
static bool s_built[KEY_LANGS][2][KEY_PAGES];

// One key's control word, as LVGL's own maps write it (lv_keyboard.c 8.3.11,
// lines 55 to 86). Its LV_KB_BTN() is defined in lv_keyboard.c alone, so the
// popover bit of a letter is spelled out here. LVGL's signs on the letter
// pages carry that bit as well; KF_CHECKED leaves it out, which shows the
// same: no keyboard here turns popovers on, and while they are off
// lv_keyboard_update_ctrl_map() strips the bit from every key.
static lv_btnmatrix_ctrl_t keyCtrl(uint8_t flag, uint8_t width) {
  switch (flag) {
    case KF_LETTER:  return (lv_btnmatrix_ctrl_t)(LV_BTNMATRIX_CTRL_POPOVER | width);
    case KF_CHECKED: return (lv_btnmatrix_ctrl_t)(LV_BTNMATRIX_CTRL_CHECKED | width);
    case KF_CTRL:    return (lv_btnmatrix_ctrl_t)(LV_KEYBOARD_CTRL_BTN_FLAGS | width);
    default:         return (lv_btnmatrix_ctrl_t)width;   // KF_PLAIN, the space bar
  }
}

static KeyLang keyLang() {
  if (g_lang == LANG_DE) return KL_DE;
  if (g_lang == LANG_FR) return KL_FR;
  return KL_EN;
}

// The VALUE_CHANGED handler of the French text keyboards, in place of
// LVGL's: the accent key opens the accent page, everything else goes on to
// lv_keyboard_def_event_cb(), so the page keys, delete, the arrows and the
// READY and CANCEL it sends for OK and the keyboard key work as before.
// Wurzel: lv_keyboard_def_event_cb() knows three page keys by their text
//   ("abc", "ABC", "1#") and types every other key into the field, so the
//   accent key on its own would type its two letters.
// Fix: this handler takes that one key and calls lv_keyboard_set_mode() with
//   LV_KEYBOARD_MODE_USER_1, where keyMapsApply() put the page.
// NOT-TO-DO: a second handler next to LVGL's: both run on the same press,
//   and LVGL's still types the two letters. Labelling the accent key "1#"
//   or "ABC" to borrow LVGL's switch: those two already lead to the special
//   and the upper page.
// See test_key_layout.py (the key and the page), test_keyboards.py.
static void keyAccentsCb(lv_event_t* e) {
  lv_obj_t* kb = lv_event_get_target(e);
  const uint16_t id = lv_btnmatrix_get_selected_btn(kb);
  const char* txt = (id == LV_BTNMATRIX_BTN_NONE) ? nullptr : lv_btnmatrix_get_btn_text(kb, id);
  if (txt && strcmp(txt, KEY_ACCENTS) == 0) {
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
    return;
  }
  lv_keyboard_def_event_cb(e);
}

// Wurzel: LVGL 8.3.11 keeps the maps of lv_keyboard_set_map() in one table
//   for all keyboards (kb_map[] and kb_ctrl[], lv_keyboard.c lines 101 to
//   122, written at line 218), not in the keyboard. The WiFi password set
//   its pages there, without the keyboard key, and every keyboard opened
//   after it had them until a restart: "New spool type" lost its only way
//   back.
// Fix: every keyboard sets all of its pages right after it is made, from
//   the tables of its language; nothing is left from the one before.
// NOT-TO-DO: putting LVGL's pages back when the WiFi screen closes: those
//   are English, not the scale's language, and every way that screen
//   closes would need it. Control words on the stack: LVGL copies
//   kb_ctrl[mode] again on every page switch (lv_keyboard_update_ctrl_map()),
//   long after this returned. Setting the number page: the weights keep
//   LVGL's own pad, which nothing replaces.
// See test_keyboards.py, test_key_layout.py.
void keyMapsApply(lv_obj_t* kb, bool wifi) {
  if (!kb) return;
  const KeyLang lang = keyLang();
  const int set = wifi ? 1 : 0;
  for (int p = 0; p < KEY_PAGES; p++) {
    KeyMap m = {};
    if (!keyLayoutGet(lang, (KeyPage)p, wifi, &m)) continue;   // the accents: French text only
    // Cannot be: key_layout.cpp holds every page to the limit at compile time.
    if (m.count > KEY_LAYOUT_MAX_KEYS) {
      logSDf("UI: keyboard page %d/%d/%d has %d keys, more than %d", (int)lang, set, p, m.count,
             KEY_LAYOUT_MAX_KEYS);
      continue;
    }
    lv_btnmatrix_ctrl_t* ctrl = s_ctrl[lang][set][p];
    if (!s_built[lang][set][p]) {
      for (int i = 0; i < m.count; i++) ctrl[i] = keyCtrl(m.flags[i], m.widths[i]);
      s_built[lang][set][p] = true;
    }
    // LVGL only reads the keys; its prototype takes them without the const.
    lv_keyboard_set_map(kb, KEY_MODE[p], const_cast<const char**>(m.keys), ctrl);
  }
  // LVGL's handler is added by the keyboard's constructor; it goes only where
  // the accent key is. Ours is taken off first, so a second call does not
  // add it twice.
  if (lang == KL_FR && !wifi) {
    lv_obj_remove_event_cb(kb, lv_keyboard_def_event_cb);
    lv_obj_remove_event_cb(kb, keyAccentsCb);
    lv_obj_add_event_cb(kb, keyAccentsCb, LV_EVENT_VALUE_CHANGED, NULL);
  }
}
