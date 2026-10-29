#pragma once

#include <lvgl.h>

// ============================================================
//  THE KEYS OF A TEXT KEYBOARD ON THE PANEL (w02, V43)
//
//  Every text keyboard calls keyMapsApply() right after lv_keyboard_create():
//  it gets the pages of the scale's language from services/key_layout.h, the
//  WiFi password its own. lv_keyboard_set_map() is called here and nowhere
//  else (test_keyboards.py).
// ============================================================

// Sets the lower, upper and special page of the language g_lang (German and
// French by their own tables, every other language English) on kb, for the
// text keyboards (wifi false) or the WiFi password (wifi true); French text
// keyboards get the accent page as well, and the handler that opens it. The
// number pad stays LVGL's own. Once per keyboard, right after it is made.
void keyMapsApply(lv_obj_t* kb, bool wifi);
