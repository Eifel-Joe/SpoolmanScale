#pragma once

// The live weights of the main screen, between the spool card and the button
// bar: the filament netto with its diff to the database, the total on the
// pad, and with a bag the netto without it. Built once by buildUI(), only
// with a load cell. The labels it fills are the globals in app/app_state.h.
void buildLiveZone();
// Swaps the labels for the other layout after the bag weight crossed 0 g.
// From the loop (live_zone_rebuild_pending): it deletes labels the loop
// writes into. Does nothing on a device built without a load cell.
void rebuildLiveZone();
