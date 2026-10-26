#pragma once

#include <stdint.h>

// ============================================================
//  AUTOMATIC SENDING: WHAT A SPOOL THAT LIES STILL BECOMES
//
//  Once a spool has lain still for AUTO_WEIGHT_STABLE_MS the automatic
//  weighing compares its filament with what the backend holds and decides
//  one of three things (the original's rule, 26.10.2026):
//
//    rise       More than AUTO_WEIGHT_RISE_G (5 g) heavier than stored is
//               held back, and the weight button asks. Filament only ever
//               leaves a spool; more weight means a bag, another spool or a
//               refill, and that is the user's call.
//
//    unchanged  Less than AUTO_WEIGHT_MIN_CHANGE_G (1 g) off either way is
//               the same value, and a write would only cost a request.
//
//    write      Anything else, lighter always.
//
//  The thresholds are the original's, from auto_weight_state.h. What is ours
//  is the reading they are applied to: the reading without the parts on the
//  spool (spool_extras_rules.h), what the scale would show for the spool
//  alone. On the raw reading a clip or a tag holder counts as filament: a
//  3 g holder would be written as 3 g of filament the spool does not have,
//  and parts of more than 5 g would hold back every weighing as a rise. The
//  weight popup's button sends the same bare value.
//
//  The rule lives here, without Arduino, NVS or the screens, so that a host
//  test holds it to the values in test_auto_send_rules.py; app_loop.cpp hands
//  in the reading, the parts for the spool, its empty weight and what the
//  backend holds, and acts on the verdict.
// ============================================================

enum AutoSendKind : uint8_t {
  AUTO_SEND_WRITE,       // written
  AUTO_SEND_UNCHANGED,   // within AUTO_WEIGHT_MIN_CHANGE_G of the backend: nothing written
  AUTO_SEND_RISE         // more than AUTO_WEIGHT_RISE_G heavier: held back, a tap asks
};

struct AutoSendVerdict {
  AutoSendKind kind;
  float netto_g;   // the filament: the reading less the parts and the empty spool, never below 0
  float diff_g;    // netto_g less what the backend holds
};

// gross_g: the pad's reading; parts_g: what hangs on the spool besides its
// filament; spool_g: its empty weight; stored_g: the filament the backend
// holds. All in grams.
AutoSendVerdict autoSendVerdict(float gross_g, float parts_g, float spool_g, float stored_g);
