#pragma once

void hideAllOverlays();
void showMainScreen();
void showSettingsScreen();
// Frees the setup's screens before its scale step. From the loop only.
void releaseSetupScreens();
// The three OTA screens go together, and two of them carry labels written from
// outside - the web upload and the parked GitHub check - that have to go with
// the screen. Every delete of the three goes through here.
void deleteOtaScreens();
