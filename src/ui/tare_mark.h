#pragma once

#include <lvgl.h>

// The "A" on the TARE button that marks auto-tare as switched on. The zero
// can move by itself then, and the button whose job that is should show it.
//
// tareMarkCreate() hangs the label on the button and shows it according to
// g_auto_tare; tareMarkShow() follows the menu switch. The latter is safe from
// an event handler: it only toggles the hidden flag of a label that lives as
// long as the main screen. Without a load cell there is no button, and both
// do nothing.
void tareMarkCreate(lv_obj_t* btn_tare);
void tareMarkShow(bool on);
