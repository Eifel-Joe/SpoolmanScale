#include "ams_pick.h"

#include <Arduino.h>
#include <string.h>

#include "app/app_state.h"
#include "hardware/sd_logger.h"
#include "services/auto_weight_state.h"
#include "services/backend.h"
#include "services/backend_api.h"
#include "services/klipper_printers.h"
#include "services/server_reach.h"
#include "services/location_state.h"
#include "services/user_options.h"
#include "ui/ams_view.h"
#include "ui/info_popup.h"
#include "ui/more_info_screen.h"

#include "lang.h"

namespace {

struct Pending {
  bool          active;
  int           spool_id;
  char          name[32];
  unsigned long noted_ms;
  // Opened by a tap (the "Into AMS" button) and not by a removal. Only
  // amsPickOpenFor() sets it; offerLocationAfterPick() reads it.
  bool          by_tap;
};

Pending s_pending;

}  // namespace

bool amsPickActive() {
  // One switch per backend: on Spoolman the question goes to an AMS source,
  // which is a different server with its own side effects on the printer.
  const bool ask = backendIsBamBuddy() ? g_ams_pick_ask : g_sm_ams_pick_ask;
  return ask && backendCanAssignAmsSlot();
}

void amsPickNote(int spool_id, const char* spool_name) {
  if (spool_id <= 0) return;
  s_pending.active   = true;
  s_pending.spool_id = spool_id;
  s_pending.noted_ms = millis();
  // Every note taken here is for a removal; the tap path says otherwise
  // after it, in amsPickOpenFor().
  s_pending.by_tap   = false;
  s_pending.name[0]  = '\0';
  if (spool_name) {
    strncpy(s_pending.name, spool_name, sizeof(s_pending.name) - 1);
    s_pending.name[sizeof(s_pending.name) - 1] = '\0';
  }
}

bool amsPickHasPending()    { return s_pending.active; }
int  amsPickPendingSpoolId(){ return s_pending.active ? s_pending.spool_id : -1; }

unsigned long amsPickPendingAgeMs() {
  return s_pending.active ? (millis() - s_pending.noted_ms) : 0;
}

void amsPickDropPending() { s_pending.active = false; }

// The location question the picker took the removal event away from. A
// spool that went into a bay needs no shelf; one whose picker was closed
// without a tap is going somewhere else, and that is what the location
// prompt is for. Same conditions and same guard as the AMS question's "no"
// branch in ams_assign_popup.cpp: the NFC poll kept running under the page,
// and a spool that landed meanwhile moved sm_id.
//
// Wurzel: the question stands in for a spool that was lifted off the dish. The
// picker that the "Into AMS" button under "More info" opens (by_tap) was not
// triggered by a removal at all, the spool may well still lie on the dish, and
// closing the page without a bay is no sign that it is going to a shelf.
// Fix: the remembered order says by_tap, and a tap-opened picker that closes
// without an answer asks nothing. The caller passes the flag along, since it
// drops the order before it asks.
// NOT-TO-DO: reading s_pending in here, the order is gone by then. Skipping
// every unanswered picker would lose the question after a removal, which is
// what it is for.
// Without a load cell the first line below already returns, so the button
// there (amsPickOpenFor() from amsMainAssignRun()) behaves as it did.
// See test_ams_info.py.
static void offerLocationAfterPick(int spool_id, bool by_tap) {
  // Without a load cell nothing is asked on its own: the location has its
  // own button there, and the picker was opened by a tap, not by a removal.
  if (!g_scale_fitted) return;
  if (by_tap) {
    logSDf("LOC: not offered after the picker, it was opened by a tap, id=%d",
           spool_id);
    return;
  }
  if (!g_auto_loc_popup || !wifi_ok) return;
  if (!sm_found || sm_archived || sm_id != spool_id) {
    logSDf("LOC: not offered after the picker, id=%d sm_id=%d found=%d archived=%d",
           spool_id, sm_id, (int)sm_found, (int)sm_archived);
    return;
  }
  if (g_loc_popup_shown_for_id == spool_id) return;
  g_loc_popup_shown_for_id = spool_id;
  logSDf("LOC: offered after the picker was closed, id=%d", spool_id);
  requestLocationPicker(true);
}

// The Spoolman location for a bay's unit: "X1C AMS 1", "X1C AMS HT", "X1C Ext".
// Language neutral on purpose - it lands in the database, where a German and
// an English scale would otherwise name one shelf twice. One location per
// unit, not per bay: the bay is what the AMS view is for.
#define AMS_HT_FIRST_ID 128
void amsLocationName(char* out, size_t n, const char* printer, int ams_id) {
  char unit[16];
  if (ams_id == AMS_EXT_AMS_ID)        snprintf(unit, sizeof(unit), "Ext");
  else if (ams_id == AMS_HT_FIRST_ID)  snprintf(unit, sizeof(unit), "AMS HT");
  else if (ams_id > AMS_HT_FIRST_ID)   snprintf(unit, sizeof(unit), "AMS HT %d", ams_id - AMS_HT_FIRST_ID + 1);
  else                                 snprintf(unit, sizeof(unit), "AMS %d", ams_id + 1);
  if (printer && printer[0]) snprintf(out, n, "%s %s", printer, unit);
  else                       snprintf(out, n, "%s", unit);
}

// After an assignment through the AMS source: the unit becomes the spool's
// location, so Spoolman's own interface knows where it is. A failure here
// costs only that - the bay is assigned either way - and is logged.
static void setAmsLocation(int spool_id, int ams_id) {
  if (backendMode() != BACKEND_SPOOLMAN || !g_sm_ams_loc) return;
  char loc[64];
  amsLocationName(loc, sizeof(loc), amsViewPrinterName(), ams_id);
  const int code = backendPatchSpoolLocation(cfg_spoolman_base, spool_id, loc, 8000);
  logSDf("AMSPICK: location of spool %d -> '%s', HTTP %d", spool_id, loc, code);
  if (!backendWriteOk(code)) return;
  if (sm_found && sm_id == spool_id) {
    snprintf(sm_location_name, sizeof(sm_location_name), "%s", loc);
    sm_location_id = 0;
  }
}

// What a refused or failed assignment says. A Klipper printer is told by its
// own codes (klipper_rules.h), then by the ones Moonraker and the transport
// give; they come first, before the test of the backend's mode below: a
// Klipper printer is assigned in Bambuddy's mode as well as in Spoolman's, and
// its codes mean nothing to either. Then what an AMS source's answer means: the
// contract names these codes; BamBuddy's own answers keep the one message they
// always had.
static int assignFailText(int code, bool klipper) {
  if (klipper) {
    switch (code) {
      case KL_E_NOT_HELD:       return STR_AMSV_KL_NOT_HELD;
      case KL_E_OTHER_SPOOLMAN: return STR_AMSV_KL_OTHER_SM;
      case KL_E_NOT_TESTED:     return STR_AMSV_KL_NOT_TESTED;
      case 401:
      case 403:                 return STR_AMSV_KL_ACCESS;
      case KL_E_NO_FIELD:
      case KL_E_NO_PRINTER:     return STR_AMSV_ASSIGN_FAIL;
      default: break;
    }
    // No transport (-1, -2 and the other client codes, KL_E_PARSE among them:
    // an answer that cannot be read is no answer), a timeout, or Klipper not
    // ready behind a Moonraker that does answer.
    if (code <= 0 || code == 408 || code == 503 || code == 504) return STR_AMSV_KL_NO_ANSWER;
    return STR_AMSV_ASSIGN_FAIL;
  }
  if (backendMode() != BACKEND_SPOOLMAN) return STR_AMSV_ASSIGN_FAIL;
  switch (code) {
    case 403: return STR_AMSV_ERR_READONLY;
    case 404: return STR_AMSV_ERR_UNKNOWN;
    case 502: return STR_AMSV_ERR_INVENTORY;
    case 503: return STR_AMSV_ERR_OFFLINE;
    default:  return STR_AMSV_ASSIGN_FAIL;
  }
}

// Runs one pass after the picker closed, never from the tap itself.
static void onPicked(int ams_id, int tray_id) {
  // The page went away without a tap: back, a backend switch, a navigation
  // that tore it down. The bay question was seen and not answered, so it is
  // not asked again for this spool - the location question still is, because
  // a spool that is not going into the printer is going onto a shelf.
  if (ams_id < 0 || tray_id < 0) {
    const int  spool_id = amsPickPendingSpoolId();
    const bool by_tap   = s_pending.active && s_pending.by_tap;   // before the drop
    logSD("AMSPICK: picker closed without an answer, note dropped");
    amsPickDropPending();
    offerLocationAfterPick(spool_id, by_tap);
    return;
  }

  const int spool_id = amsPickPendingSpoolId();
  if (spool_id <= 0) {
    logSD("AMSPICK: answer arrived with no spool remembered");
    return;
  }
  // Another spool can have landed on the pad while the question stood. The
  // answer belongs to the one that was asked about, or to nothing.
  if (sm_found && sm_id > 0 && sm_id != spool_id) {
    logSDf("AMSPICK: answer discarded, parked spool changed from %d to %d",
           spool_id, sm_id);
    amsPickDropPending();
    return;
  }

  const int printer_id = amsViewPrinterId();
  if (printer_id <= 0) {
    logSD("AMSPICK: no printer to assign to");
    amsPickDropPending();
    return;
  }

  // A Klipper printer: its spool numbers are Spoolman's, its bays are fields of
  // the printer, and what it answers is not a verdict on Bambuddy or Spoolman.
  const bool klipper = klipperIsPrinterId(printer_id);

  // Where it sits now, so the result can say "moved" rather than "assigned"
  // when it was already somewhere. A failure here is not fatal: it only
  // costs the nicer wording. Not for a Klipper printer: Bambuddy's assignment
  // list knows nothing of it, and the wording stays "assigned".
  int was_ams = -1, was_tray = -1;
  if (!klipper) backendFindSpoolSlot(spool_id, printer_id, &was_ams, &was_tray, 6000);

  int code = backendAssignAmsSlot(spool_id, printer_id, ams_id, tray_id, 8000);
  // Root: serverReachNote() marks Bambuddy or Spoolman as down for -1, -11 or
  // 503, which a Klipper printer that is switched off answers just the same;
  // the banner would blame two servers that are fine (R27, test_ams_klipper.py).
  // NOT-TO-DO: noting it and taking the banner back later.
  if (!klipper) code = serverReachNote(code, true);
  amsPickDropPending();

  if (code == 200) {
    const bool moved = (was_ams >= 0 && (was_ams != ams_id || was_tray != tray_id));
    logSDf("AMSPICK: spool %d -> printer %d bay %d/%d (%s)",
           spool_id, printer_id, ams_id, tray_id, moved ? "moved" : "new");
    // klipperAssign() sets the location itself (the printer's name, when the
    // setting is on and Happy Hare does not write it): a second write here
    // would put the AMS source's "<printer> AMS 1" over it.
    if (!klipper) setAmsLocation(spool_id, ams_id);
    showInfoPopup(STR_AMSV_TITLE, moved ? STR_AMSV_MOVED : STR_AMSV_ASSIGNED,
                  INFO_DONE);
    // A spool going into the printer does not also need the storage location
    // question on top of it.
    g_loc_popup_shown_for_id = spool_id;
  } else {
    // The code goes to the log, not into the popup: showInfoPopup takes
    // string ids so the handler can still read them after the caller's
    // buffers are gone, and a number cannot ride along in one.
    logSDf("AMSPICK: assignment of spool %d failed, HTTP %d", spool_id, code);
    showInfoPopup(STR_AMSV_TITLE, assignFailText(code, klipper), INFO_WARN);
  }
}

// The page itself, headed with the remembered spool's name.
static void openPicker() {
  char fmt[48], head[80];
  copyT(fmt, sizeof(fmt), STR_AMSV_PICK_HEAD);
  snprintf(head, sizeof(head), fmt,
           s_pending.name[0] ? s_pending.name : "");
  requestAmsView(AMS_VIEW_PICK, onPicked, head);
}

void amsPickShow() {
  if (!s_pending.active) return;
  // A note taken under one backend can be asked about under another only if
  // that one can assign bays too, and the option is still on.
  if (!amsPickActive()) {
    amsPickDropPending();
    return;
  }

  openPicker();
}

void amsPickOpenFor(int spool_id, const char* spool_name) {
  if (spool_id <= 0 || !backendCanAssignAmsSlot()) return;
  amsPickNote(spool_id, spool_name);
  s_pending.by_tap = true;          // after the note, which clears it
  logSDf("AMSPICK: picker opened by the AMS button for id=%d", spool_id);
  openPicker();
}

void amsPickTick() {
  if (!s_pending.active) return;
  if (isAmsViewOpen()) return;          // the question is on screen, let it stand
  if (amsPickPendingAgeMs() <= AMS_PICK_MAX_MS) return;
  logSDf("AMSPICK: note for spool %d expired", s_pending.spool_id);
  amsPickDropPending();
}
