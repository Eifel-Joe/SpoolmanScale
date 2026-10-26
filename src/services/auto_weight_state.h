#pragma once

extern bool g_auto_weight;
extern bool g_auto_loc_popup;
extern bool g_auto_tare;
extern float auto_weight_last_val;
extern unsigned long auto_weight_stable_ms;
// This spool is dealt with - written, found unchanged, or held back - until
// it comes off the pad. A send from the weight popup sets it too.
extern bool auto_weight_done;

#define AUTO_WEIGHT_STABLE_MS 3000
#define AUTO_WEIGHT_THRESH_G 0.5f
// Below this much change against the backend nothing is written: the value
// is the same, and a write would only cost a request.
#define AUTO_WEIGHT_MIN_CHANGE_G 1.0f
// A spool that reads this much heavier than stored is not written on its
// own. Filament only ever leaves a spool; more weight means a bag, another
// spool, or a refill, and the user is asked (Nikolai, 26.10.2026: 5 g).
#define AUTO_WEIGHT_RISE_G 5.0f
