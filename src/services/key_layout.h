#pragma once

#include <stdint.h>

// ============================================================
//  THE KEYS OF THE TEXT KEYBOARDS: THE TABLES (w02, V43)
//
//  The text keyboards follow the language of the scale: English is LVGL's
//  own layout, German QWERTZ with the umlauts and the sharp s, French AZERTY
//  with four accents on the letter pages and a page of the others. The WiFi
//  password has the letters of the language without umlauts or accents, a
//  special page with more signs and no keyboard key.
//
//  The tables live here without Arduino or LVGL, so a host test holds them
//  key by key to the values in test_key_layout.py; ui/key_maps.cpp turns
//  them into LVGL's maps (keyMapsApply()).
// ============================================================

enum KeyLang : uint8_t { KL_EN = 0, KL_DE, KL_FR };
// KP_ACCENTS: the French page of the accents (LVGL's LV_KEYBOARD_MODE_USER_1).
enum KeyPage : uint8_t { KP_LOWER = 0, KP_UPPER, KP_SPECIAL, KP_ACCENTS };
// What a key is, as LVGL's own maps set it (lv_keyboard.c 8.3.11, lines 55
// to 86): a letter or a sign that is typed; a key drawn in the checked style
// (delete, new line, the arrows and the signs of the letter pages); a control
// key (page switch, keyboard key, OK, the accent page key); the space bar,
// which carries its width alone.
enum KeyFlag : uint8_t { KF_LETTER = 0, KF_CHECKED = 1, KF_CTRL = 2, KF_PLAIN = 3 };

// The control keys, spelled as LVGL 8.3.11 spells them (lv_symbol_def.h:
// LV_SYMBOL_BACKSPACE line 264, NEW_LINE 272, KEYBOARD 212, LEFT 124, RIGHT
// 128, OK 48). lv_keyboard_def_event_cb() acts on these texts, so a key must
// carry exactly them; ui/key_maps.cpp holds each to the LV_SYMBOL_* of the
// build with a static_assert. The page keys are LVGL's texts as well: "1#",
// "ABC" and "abc".
#define KEY_SYM_BACKSPACE  "\xEF\x95\x9A"
#define KEY_SYM_NEW_LINE   "\xEF\xA2\xA2"
#define KEY_SYM_KEYBOARD   "\xEF\x84\x9C"
#define KEY_SYM_LEFT       "\xEF\x81\x93"
#define KEY_SYM_RIGHT      "\xEF\x81\x94"
#define KEY_SYM_OK         "\xEF\x80\x8C"
// The key of the French accent page: the a grave and the e acute.
#define KEY_ACCENTS        "\xC3\xA0\xC3\xA9"

// The most keys on a page, row breaks not counted. key_layout.cpp holds
// every page to it with a static_assert.
#define KEY_LAYOUT_MAX_KEYS  48

// The bits of a key's control word as LVGL 8.3.11 has them (lv_btnmatrix.h
// lines 36 to 42, lv_keyboard.h line 32), spelled here so the tables need no
// LVGL; ui/key_maps.cpp holds each to LVGL's own with a static_assert. The
// width is the low four bits.
#define KEY_CTRL_NO_REPEAT   0x0020
#define KEY_CTRL_CHECKED     0x0100
#define KEY_CTRL_CLICK_TRIG  0x0200
#define KEY_CTRL_POPOVER     0x0400
#define KEY_CTRL_BTN_FLAGS   (KEY_CTRL_NO_REPEAT | KEY_CTRL_CLICK_TRIG | KEY_CTRL_CHECKED)

// One key's control word, as LVGL's own maps write it (lv_keyboard.c 8.3.11,
// lines 55 to 86). Its LV_KB_BTN() is defined in lv_keyboard.c alone, so the
// popover bit of a letter is spelled out here. LVGL's signs on the letter
// pages carry that bit as well; KF_CHECKED leaves it out, which shows the
// same: no keyboard here turns popovers on, and while they are off
// lv_keyboard_update_ctrl_map() strips the bit from every key.
constexpr uint16_t keyCtrlWord(uint8_t flag, uint8_t width) {
  return flag == KF_LETTER  ? (uint16_t)(KEY_CTRL_POPOVER | width)
       : flag == KF_CHECKED ? (uint16_t)(KEY_CTRL_CHECKED | width)
       : flag == KF_CTRL    ? (uint16_t)(KEY_CTRL_BTN_FLAGS | width)
       :                      (uint16_t)width;   // KF_PLAIN, the space bar
}

// One page. keys in the format of lv_btnmatrix_set_map(): the rows split by
// "\n", "" at the end. widths, flags and ctrl hold one entry per key, without
// the row breaks; ctrl is each key's control word (keyCtrlWord()), the map
// lv_keyboard_set_map() takes. count is the number of keys. All of it is
// constant, in flash, so LVGL may keep the pointers.
struct KeyMap {
  const char* const* keys;
  const uint8_t* widths;
  const uint8_t* flags;
  const uint16_t* ctrl;
  int count;
};

// The page of a language for the text keyboards (wifi false) or for the WiFi
// password (wifi true). False, and *out untouched, for a page that is not
// there: KP_ACCENTS is French and not for the WiFi password. Every other page
// is there for every language. A null out only asks.
bool keyLayoutGet(KeyLang lang, KeyPage page, bool wifi, KeyMap* out);
