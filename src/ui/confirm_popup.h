#pragma once

#include <stdint.h>

// The question where an empty spool weight goes (showSpoolWeightScope(),
// isSpoolWeightScopeOpen()) lives in a file of its own; callers keep
// including this header for it.
#include "tare_scope_popup.h"

void closeConfirmPopup();
void showConfirmPopup(const char* msg, int action);
bool isConfirmPopupOpen();
// Where a parked empty spool weight is written. The buttons of the scope
// question hand one over with confirmParkTare() and appLoop() runs it.
enum TareScope : uint8_t { TARE_SCOPE_SPOOL, TARE_SCOPE_FILAMENT, TARE_SCOPE_VENDOR };
// Parks an empty spool weight for the spool it was asked for. then_new_spool:
// the initial weight follows it (the New spool flow).
void confirmParkTare(TareScope scope, float grams, int spool_id, bool then_new_spool);
// Parks the full spool of the empty spool button (design L14) for the spool
// it was asked for. tare is what was picked: a type, or the weight computed
// from the fill. It is written first; only once the server has it do the
// label weight (the fill, or the rest where that is more) and the rest
// (bare minus tare) follow. bare is the reading the fill was taken with.
void confirmParkFullSpool(float tare, float fill, float bare, int spool_id);
// Runs whatever a popup button parked - the weight, tare, archive and cap
// writes. Called from appLoop().
void handleConfirmPopupDeferredActions();
// Every dialog this file owns, for the navigation. Closed, not hidden: a
// question nobody can see any more must not keep uiModalWaiting() true.
void closeConfirmPopups();

// Asked when BamBuddy's own inventory would clamp the measurement to the
// label weight. Built from appLoop(), never from a write path.
void showBamBuddyCapPopup(float measured_g, float label_g);
