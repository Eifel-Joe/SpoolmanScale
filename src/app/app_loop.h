#pragma once

void appLoop();

// Clears the main screen of a tag that was lifted (labels, the tag's ids, the
// buttons) and ends the wait of the no-tag timer, as the timer and the clear
// button do. Also the first step of "Create spool" while the lifted tag's
// leftovers are still up. Loop task only.
void dropLiftedTagDisplay();
