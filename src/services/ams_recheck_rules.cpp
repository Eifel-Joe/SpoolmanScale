#include "ams_recheck_rules.h"

const uint32_t AMS_RECHECK_AT_MS[AMS_RECHECK_ROUNDS] = {150000u, 300000u, 480000u};

void amsRecheckClear(AmsRecheck* t, int n) {
  for (int i = 0; i < n; i++) t[i] = AmsRecheck{};
}

int amsRecheckStart(AmsRecheck* t, int n, const AmsRecheck& want, uint32_t now_ms) {
  const int spool_id = want.spool_id, printer_id = want.printer_id;
  const int ams_id = want.ams_id, tray_id = want.tray_id;
  if (!t || n <= 0 || spool_id <= 0 || printer_id <= 0 || ams_id < 0 || tray_id < 0) return -1;
  int use = -1;
  for (int i = 0; i < n; i++) {
    if (t[i].active && t[i].spool_id == spool_id) { use = i; break; }
  }
  // Another spool's watch on this bay is over: BamBuddy holds this one there
  // now, and that watch would only find "another spool" and end anyway.
  for (int i = 0; i < n; i++) {
    if (i == use || !t[i].active) continue;
    if (t[i].printer_id == printer_id && t[i].ams_id == ams_id && t[i].tray_id == tray_id) {
      if (use < 0) use = i;
      else t[i].active = false;
    }
  }
  if (use < 0) {
    for (int i = 0; i < n; i++) {
      if (!t[i].active) { use = i; break; }
    }
  }
  if (use < 0) {
    // All taken: the watch that has run longest goes, it is nearest its end.
    use = 0;
    for (int i = 1; i < n; i++) {
      if ((uint32_t)(now_ms - t[i].since_ms) > (uint32_t)(now_ms - t[use].since_ms)) use = i;
    }
  }
  AmsRecheck& r = t[use];
  r.active     = true;
  r.spool_id   = spool_id;
  r.printer_id = printer_id;
  r.ams_id     = ams_id;
  r.tray_id    = tray_id;
  r.since_ms   = now_ms;
  r.round      = 0;
  r.resent     = 0;
  return use;
}

int amsRecheckStop(AmsRecheck* t, int n, int spool_id) {
  int stopped = 0;
  for (int i = 0; t && i < n; i++) {
    if (t[i].active && t[i].spool_id == spool_id) {
      t[i].active = false;
      stopped++;
    }
  }
  return stopped;
}

int amsRecheckDue(const AmsRecheck* t, int n, uint32_t now_ms) {
  for (int i = 0; t && i < n; i++) {
    const AmsRecheck& r = t[i];
    if (!r.active || r.round >= AMS_RECHECK_ROUNDS) continue;
    if ((uint32_t)(now_ms - r.since_ms) >= AMS_RECHECK_AT_MS[r.round]) return i;
  }
  return -1;
}

void amsRecheckLooked(AmsRecheck& r) {
  if (r.round < AMS_RECHECK_ROUNDS) r.round++;
  if (r.round >= AMS_RECHECK_ROUNDS) r.active = false;
}

AmsRecheckStep amsRecheckAfterFind(const AmsRecheck& r, int code, int found_ams, int found_tray) {
  if (code != 200) return RC_NO_ANSWER;
  if (found_ams == r.ams_id && found_tray == r.tray_id) return RC_STILL_THERE;
  if (found_ams >= 0) return RC_MOVED;
  return RC_GO_ON;
}

AmsRecheckStep amsRecheckAfterBay(const AmsRecheck& r, int other_spool) {
  if (other_spool > 0 && other_spool != r.spool_id) return RC_OTHER_SPOOL;
  return RC_GO_ON;
}

AmsRecheckStep amsRecheckAfterPresence(const AmsRecheck& r, bool known, bool present) {
  if (!known) return RC_BAY_UNKNOWN;
  if (!present) return RC_BAY_EMPTY;
  if (r.resent >= AMS_RECHECK_RESEND_MAX) return RC_GIVE_UP;
  return RC_RESEND;
}

bool amsRecheckStepEnds(AmsRecheckStep s) {
  return s == RC_MOVED || s == RC_OTHER_SPOOL || s == RC_GIVE_UP;
}
