#pragma once

void showMoreInfoScreen();
void requestLocationPicker(bool from_popup);
// The location button itself. Like requestLocationPicker(), but it opens the
// picker for an archived spool too: tapped, the location is asked for. The
// automatic offers call requestLocationPicker() and leave archived spools out.
void tapLocationButton(bool from_popup);
// The picker for a spool that is not on the pad: one just taken out of an AMS
// bay. Writes to spool_id instead of the spool on the pad, marks `current` as
// the location it has now, and returns to the main screen when done. With
// clear_on_cancel a picker closed without a choice empties the location.
void requestLocationPickerFor(int spool_id, const char* current, bool clear_on_cancel);
// The picker as requestLocationPicker(true) asks for it (its ways out go to the
// main screen, with Cancel and its countdown), for a caller that waits for its
// end: ended(spool_id) runs once when it ended by itself - a row or "no
// location" chosen, the X, Cancel, the countdown, or not shown at all - after
// its own way back to the main screen. Not when a navigation took it down
// (hideAllOverlays()) or another request replaced it. Only flags; ended runs
// from the loop.
void requestLocationPickerThen(int spool_id, void (*ended)(int spool_id));
// Whether a requestLocationPickerThen() with this ended still waits: asked
// for or up, not yet ended, not taken down by a navigation, not replaced.
bool locationPickerThenPending(void (*ended)(int spool_id));
void handleMoreInfoDeferredActions();

// Both More Info pickers, released from hideAllOverlays(). Without this a
// picker left open survives a navigation change and sits on top of whatever
// comes next.
void hideMoreInfoOverlays();
// Frees the unlink confirmation, for the navigation.
void closeMoreInfoPopups();
// Parks the same unlink the More Info button runs, for a spool that was just
// archived. all frees every tag of the spool, erase_now erases the tag on the
// reader without asking again, and only when it is still erase_uid, the tag
// the question named. Only flags, so safe from an LVGL callback.
void requestSpoolUnlink(int spool_id, bool all, bool erase_now, const char* erase_uid);
