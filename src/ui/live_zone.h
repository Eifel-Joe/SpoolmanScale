#pragma once

#include <lvgl.h>

// The live weights of the main screen, between the spool card and the button
// bar: the filament netto with its diff to the database, the total on the
// pad, and with a bag the netto without it. Built once by buildUI(), only
// with a load cell. The labels it fills are the globals in app/app_state.h.
void buildLiveZone();
// Swaps the labels for the other layout after the bag weight crossed 0 g.
// From the loop (live_zone_rebuild_pending): it deletes labels the loop
// writes into. Does nothing on a device built without a load cell.
void rebuildLiveZone();
// Writes a diff of the zone (lbl_raw_info, lbl_bag_sm_diff) in the largest
// font, from the one the layout gave it down to 12, that keeps it inside its
// column, on the baseline it was laid out on. Its colour stays the caller's.
// Ignores a label that is not one of the two.
void liveZoneSetDiff(lv_obj_t* lbl, const char* text);
