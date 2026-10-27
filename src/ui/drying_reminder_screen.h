#pragma once

void showDryingReminderScreen();

// The numpad for one threshold (0 yellow, 1 red). From the loop only, by way
// of dry_pad_pending.
void showDryingPad(int target);
