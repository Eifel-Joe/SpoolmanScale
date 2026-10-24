#pragma once

// Which of the parts set up in the web settings (clip, RFID holder ...) hang
// on the spool on the pad, at most three. OK remembers the choice for that
// spool; OK and Cancel both close the popup and then call on_close, which
// brings back what it was opened from.
void showExtrasChooser(void (*on_close)());
bool isExtrasChooserOpen();
// Closed without calling on_close: for a navigation away.
void closeExtrasChooser();
