#pragma once

#include <stdint.h>

// ============================================================
//  THE BAY RECHECK: RULES
//
//  BamBuddy releases a bay the AMS reports empty for two minutes, in both
//  inventory modes, and a bay that was reported empty first and then
//  "occupied but unreadable" counts as a swap and is released as well
//  (maziggy/bambuddy dev, backend/app/services/slot_unlink_grace.py,
//  GRACE_SECONDS = 120; main.py, the Spoolman passes). So an assignment made
//  at the scale before the spool went into the AMS - weigh, tap the bay, put
//  it in - is gone two minutes later. Sent again while the spool sits in the
//  bay it holds: a new assignment drops BamBuddy's hold (forget_slot()).
//
//  The scale looks 2.5, 5 and 8 minutes after an assignment and sends it
//  again, twice at most, when BamBuddy no longer has it, no other spool is on
//  file for the bay and the AMS reports something in it (Jensilein,
//  Jensilein/SpoolmanScale efb7257b, second part; Spec 5.3 AC j07,
//  v0.8.7-beta.4, V71). Jensilein watched the last assignment only; this
//  watches up to eight at once, so several spools weighed before they are
//  loaded are all covered (E3).
//
//  Without Arduino, so that test_ams_recheck_rules.py holds every rule;
//  services/ams_pick.cpp asks the questions and acts on the answers.
// ============================================================

#define AMS_RECHECK_SLOTS       8    // as many bays as two AMS units have
#define AMS_RECHECK_ROUNDS      3
#define AMS_RECHECK_RESEND_MAX  2

// When the looks fall due, counted from the assignment: after BamBuddy's two
// minutes, then twice more for a spool that went in late.
extern const uint32_t AMS_RECHECK_AT_MS[AMS_RECHECK_ROUNDS];

struct AmsRecheck {
  bool     active;
  int      spool_id;
  int      printer_id;
  int      ams_id;
  int      tray_id;
  uint32_t since_ms;     // when it was assigned
  uint8_t  round;        // looks done
  uint8_t  resent;       // times sent again
};

void amsRecheckClear(AmsRecheck* t, int n);

// Watches an assignment. The spool's own watch is taken over (it moved); a
// watch of another spool on the same bay ends (the bay holds this one now);
// otherwise a free place, and when none is free the oldest watch's. The index
// used, or -1 for a spool, printer or bay that is none.
// want names the spool and the bay (spool_id, printer_id, ams_id, tray_id);
// the rest of it is not read.
int  amsRecheckStart(AmsRecheck* t, int n, const AmsRecheck& want, uint32_t now_ms);

// Ends every watch of the spool: taken out, or "not into the AMS". How many.
int  amsRecheckStop(AmsRecheck* t, int n, int spool_id);

// The first watch whose next look is due, or -1. Wraps with millis().
int  amsRecheckDue(const AmsRecheck* t, int n, uint32_t now_ms);

// One look is done: the next falls due later, and after the last the watch ends.
void amsRecheckLooked(AmsRecheck& r);

enum AmsRecheckStep : uint8_t {
  RC_GO_ON,          // ask the next question
  RC_NO_ANSWER,      // BamBuddy could not be asked, the next round looks again
  RC_STILL_THERE,    // BamBuddy has it in the bay
  RC_MOVED,          // BamBuddy has it in another bay: not ours to undo
  RC_OTHER_SPOOL,    // another spool is on file for the bay: leave it
  RC_BAY_EMPTY,      // nothing in the bay: not in yet, or gone elsewhere
  RC_BAY_UNKNOWN,    // the AMS could not be read
  RC_RESEND,         // send it again
  RC_GIVE_UP         // dropped again after two resends
};

// The spool's place as BamBuddy has it: HTTP code and bay (ams -1 nowhere).
AmsRecheckStep amsRecheckAfterFind(const AmsRecheck& r, int code, int found_ams, int found_tray);
// Who BamBuddy has in the watched bay: a spool id, 0 for none, negative for a
// lookup that failed (no answer).
AmsRecheckStep amsRecheckAfterBay(const AmsRecheck& r, int other_spool);
// What the AMS reports for the bay.
AmsRecheckStep amsRecheckAfterPresence(const AmsRecheck& r, bool known, bool present);
// Whether the step ends the watch before its last round.
bool amsRecheckStepEnds(AmsRecheckStep s);
