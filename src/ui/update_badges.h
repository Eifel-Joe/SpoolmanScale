#pragma once

#include <lvgl.h>

// Creates the red update dot and places it on the anchor's top right corner.
//
// Every screen used to position its own dot with absolute coordinates typed in
// by hand, which is why no two of them lined up: one sat flush with the top
// edge, the next two pixels below it, and the burger one was inset from the
// corner entirely. Anchoring removes the arithmetic and keeps the dot in place
// when a layout changes.
//
// The dot is a child of the anchor, so it scrolls and hides with it, and the
// anchor is allowed to draw past its own edge for the half of the dot that
// hangs outside. Call this after the anchor has its final position.
lv_obj_t* createUpdateBadge(lv_obj_t* anchor);

void showUpdateBadges(bool show);
