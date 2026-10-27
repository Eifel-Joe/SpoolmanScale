#pragma once

#include <lvgl.h>

#include "app/setup_flow.h"

// The frame every step of the first setup shares, so the steps read as one
// sequence: back on the left, the title, "n of N" on the right where a
// settings screen has its close button, and one bar at the bottom with a
// quiet answer on the left and the main one on the right.

// back_cb nullptr leaves the back button out; SETUP_STEP_NONE the count.
void setupHeader(lv_obj_t* scr, const char* title, SetupStep step, lv_event_cb_t back_cb);

// For a screen the setup shares with the settings: the setup's header while
// the setup runs, the settings' back, title and close button otherwise.
void setupOrSettingsHeader(lv_obj_t* scr, const char* title, SetupStep step,
                           lv_event_cb_t back_cb);

struct SetupBar {
  lv_obj_t* quiet;        // nullptr when the step has no quiet answer
  lv_obj_t* main;
  lv_obj_t* main_label;   // for a step that renames its way on
};

// quiet_text nullptr gives the main button the whole width.
SetupBar setupActionBar(lv_obj_t* scr, const char* quiet_text, lv_event_cb_t quiet_cb,
                        const char* main_text, lv_event_cb_t main_cb);

// The right button as an outline instead of a fill, for a step whose way on
// is a pick in a list and not a button.
void setupBarMainOutline(const SetupBar& bar);

// One line, centred in the width it is given and cut with dots beyond it.
// Unlike labelOneLine() it keeps that width, so it centres on the slot and
// not on its own text.
lv_obj_t* setupCenteredLabel(lv_obj_t* parent, const char* text, const lv_font_t* font, int w);
