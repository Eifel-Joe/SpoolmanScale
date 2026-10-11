#pragma once

// The live weights of the main screen, between the spool card and the button
// bar: the filament netto with its diff to the database, the total on the
// pad, and with a bag the netto without it. Built once by buildUI(), only
// with a load cell; a changed bag weight shows after a restart, like the
// scale switch. The labels it fills are the globals in app/app_state.h.
void buildLiveZone();
