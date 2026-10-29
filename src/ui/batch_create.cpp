#include "ui/batch_create.h"

#include <stdint.h>
#include <stdio.h>

// backend_api.h brings ArduinoJson, whose templates have a parameter T: before
// lang.h, which defines T(), and spool_flow_internal.h includes lang.h (see the
// note in tag_create_popup.cpp). app_state.h as everywhere before it.
#include "services/backend_api.h"
#include "app/app_state.h"
#include "ui/spool_flow_internal.h"
#include "app/deferred_actions.h"   // show_print_sheet_pending
#include "app/label_spool.h"
#include "hardware/sd_logger.h"
#include "services/batch_create_rules.h"
#include "services/label_printer.h"
#include "services/copy_bbs.h"
#include "services/copy_rules.h"    // CopyMissing
#include "services/server_reach.h"
#include "services/spool_cache.h"
#include "services/spool_start.h"
#include "services/spool_types.h"   // spoolTypesActive()
#include "services/tag_create.h"
#include "ui/loading_overlay.h"
#include "ui/main_screen_helpers.h"
#include "ui/no_tag_create.h"
#include "ui/price_pad.h"
#include "ui/print_card.h"
#include "ui/print_sheet.h"
#include "ui/printer_screen.h"
#include "ui/tag_create_popup.h"
#include "ui/theme.h"
#include "lang.h"

// The count button: BATCH_CHIP_EDGE from the card's top and right edge, above
// the title (UI_CARD_TITLE_Y) and clear of the icon in the middle. Its text
// gets BATCH_CHIP_W - 8 (ui_budgets.tsv, the house button rule), which holds
// the widest of the three languages with the %d as the width gate measures it.
#define BATCH_CHIP_W     100
#define BATCH_CHIP_H      36
#define BATCH_CHIP_EDGE    8

static int       s_count      = BATCH_COUNT_MIN;
// The label of the button on the card that stands, nullptr while there is none,
// and what that card does with a new count.
static lv_obj_t* s_chip_lbl   = nullptr;
static void    (*s_changed)() = nullptr;

int batchCount() { return s_count; }

void batchCountReset() { s_count = BATCH_COUNT_MIN; }

const char* batchCreateCaption(char* buf, size_t cap) {
  if (s_count > BATCH_COUNT_MIN) snprintf(buf, cap, T(STR_BATCH_CREATE_N), s_count);
  else                           snprintf(buf, cap, "%s", T(STR_COPY_CARD_CREATE));
  return buf;
}

// The button's text, in its colour.
static void chipText(const char* text, uint32_t color) {
  if (!s_chip_lbl) return;
  lv_label_set_text(s_chip_lbl, text);
  lv_obj_set_style_text_color(s_chip_lbl, lv_color_hex(color), 0);
}

static void chipShowCount() {
  char buf[32];
  snprintf(buf, sizeof(buf), T(STR_BATCH_COUNT_CHIP), s_count);
  chipText(buf, UI_COL_INK_2);
}

// Wurzel: the pad answers from the loop (pricePadTick()), a pass or more after
// the tap, and the card may have been closed in between. A card that is closed
// is hidden at once and deleted on a later pass (releaseScreen(),
// hideSpoolFlowOverlays()): the label is still there, and the copy card would
// be built again for a create nobody sees.
// Fix: the answer counts only for a button whose card is still shown, with no
// object from its label up to the screen hidden. Once deleted, the button has
// forgotten its label (the LV_EVENT_DELETE in batchCountChip()).
// NOT-TO-DO: asking the card's own pointer, as copyFillTyped() asks
// scr_copy_confirm: hideSpoolFlowOverlays() hides the card and leaves the
// pointer set until the loop deletes it.
static bool chipStanding() {
  if (!s_chip_lbl) return false;
  for (lv_obj_t* o = s_chip_lbl; o; o = lv_obj_get_parent(o))
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return false;
  return true;
}

// The pad's answer, from the loop (pricePadTick()); a cancelled pad does not
// call. An emptied field types nothing, as for the fill weight: the count
// stays as it is. The count is a whole number in range (batchCountValid());
// anything else typed takes nothing and puts the range on the button, where
// the card has room for it, and the card is not told. A valid count is taken,
// shown on the button, and the card shows it on its create button.
static void countTyped(float typed) {
  if (!noTagMode() || !chipStanding()) return;
  if (typed <= 0.0f) return;
  if (!batchCountValid(typed)) {
    logSDf("Batch: count %.2f is out of range, not taken", typed);
    chipText(T(STR_BATCH_COUNT_RANGE), UI_COL_WARN);
    return;
  }
  s_count = (int)typed;
  logSDf("Batch: %d spools to create", s_count);
  chipShowCount();
  if (s_changed) s_changed();
}

void batchCountChip(lv_obj_t* box, void (*changed)()) {
  s_changed = changed;
  lv_obj_t* btn = lv_btn_create(box);
  lv_obj_set_size(btn, BATCH_CHIP_W, BATCH_CHIP_H);
  lv_obj_set_pos(btn, UI_POPUP_W - BATCH_CHIP_EDGE - BATCH_CHIP_W, BATCH_CHIP_EDGE);
  // In the look of the price button on the same cards.
  lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COL_ROW), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COL_ROW_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(UI_COL_LINE), 0);
  lv_obj_set_style_radius(btn, UI_RADIUS_BTN, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  // Wurzel: the price pad fills its field in with "%.2f", and then refuses
  // another digit: opened with the count, it shows "1.00", and a 3 takes four
  // presses of the delete key first.
  // Fix: the pad opens empty, as for a fill weight nobody knows; the count
  // stays on the button, and an emptied field keeps it (countTyped()).
  // NOT-TO-DO: the house number pad (num_pad.h), which would show the count and
  // replace it with the first digit: it answers inside its button's event,
  // where num_pad.h allows nothing that deletes a screen, and a new count
  // builds the copy card again; its close button also leaves the create for
  // the main screen.
  // See test_batch_flow.py (the card).
  lv_obj_add_event_cb(btn, [](lv_event_t*) {
    logSD("BTN: Card -> Count");
    showPricePadInfo(0, STR_BATCH_COUNT_TITLE, STR_BATCH_COUNT_INFO, countTyped);
  }, LV_EVENT_CLICKED, NULL);
  s_chip_lbl = lv_label_create(btn);
  lv_obj_set_style_text_font(s_chip_lbl, UI_FONT_SMALL, 0);
  lv_obj_center(s_chip_lbl);
  // A card built again (the copy card, for a new count) has its new button
  // before the old one is deleted, a pass later: only the label of the button
  // being deleted is forgotten.
  lv_obj_add_event_cb(btn, [](lv_event_t* e) {
    if (lv_event_get_user_data(e) == s_chip_lbl) {
      s_chip_lbl = nullptr;
      s_changed  = nullptr;
    }
  }, LV_EVENT_DELETE, s_chip_lbl);
  chipShowCount();
}

// ------------------------------------------------------------
//  The run (batch_create.h)
// ------------------------------------------------------------

// What a stop says about the spool it stopped at, for the overview.
#define BATCH_REASON_MAX 96

// The count of the run. batchCountValid() keeps it within the rules already;
// the ids arrays below hold BATCH_COUNT_MAX, so the run does not rely on that.
static int runWanted() {
  const int wanted = batchCount();
  return wanted > BATCH_COUNT_MAX ? BATCH_COUNT_MAX : wanted;
}

// "Spool 2 of 5 ..." on the loading overlay, at = the place of the spool about
// to be made. The overlay paints before it returns (loadingOverlaySetText()
// calls lv_refr_now()), so the line is on the screen while the call holds
// the loop.
static void runProgress(int at, int wanted) {
  char buf[48];
  snprintf(buf, sizeof(buf), T(STR_BATCH_PROGRESS), at, wanted);
  loadingOverlayShow(buf);
}

// Why the run stopped at the spool in place `at`: no answer, so the spool may
// exist and the user is sent to the inventory, or the code it failed with.
static void runStopReason(BatchStep step, int at, int code, char* out, size_t cap) {
  if (step == BS_STOP_UNSURE) snprintf(out, cap, T(STR_BATCH_STOP_UNSURE), at);
  else                        snprintf(out, cap, T(STR_BATCH_STOP_FAILED), at, code);
}

// Wurzel: behind Bambuddy on Spoolman, copyBbsCreate() takes the template the
// card loaded, and the price typed on it, only while that load is held
// (copy_bbs.h); doCopySpoolCreate() frees it after its one create. Freed after
// each copy, the second would read the template again (another stall), and
// would go without the typed price.
// Fix: the load is freed once, after the loop, on every way out of the run.
// The parts each copy left behind are gathered (copyBbsLastMissing() holds the
// last create's only) and said once, as for one copy. A lost answer stops the
// run as "unsure", as on the card: the copy may exist.
// NOT-TO-DO: one copyBbsLoad() per copy: the card showed one template, and a
// read between two copies could bring other values than it showed.
// See test_batch_flow.py (the run).
void batchCopyCreate(int template_spool_id, int template_filament_id, const SpoolStart& start,
                     float tare_g, float chosen_core_g) {
  const int wanted = runWanted();
  int ids[BATCH_COUNT_MAX] = {};
  int made = 0, code = 0;
  uint8_t missing = CM_NONE;
  BatchStep step = BS_NEXT;
  while (step == BS_NEXT) {
    runProgress(made + 1, wanted);
    int new_id = 0;
    code = serverReachNote(backendCreateSpool(cfg_spoolman_base, template_spool_id, template_filament_id,
      (float)start.initial_g, tare_g, start.remaining_g, &new_id, 8000, chosen_core_g), true);
    const bool ok = (code == 200 || code == 201) && new_id > 0;
    if (ok) {
      ids[made++] = new_id;
      if (spoolTypesActive()) missing |= copyBbsLastMissing();
    }
    logSDf("Batch: copy %d of %d from template %d: HTTP %d, spool %d",
           made + (ok ? 0 : 1), wanted, template_spool_id, code, new_id);
    step = batchCreateStep(made, wanted, ok, tagCreateAnswerLost(code));
  }
  loadingOverlayHide();
  copyBbsClear();
  if (made == 0) {
    // As doCopySpoolCreate() ends a copy that failed: nothing was created,
    // the picker's hidden screens and its copy mode go.
    logSDf("Batch: no copy made, HTTP %d", code);
    linkPickerClose();
    lv_label_set_text(lbl_status, T(STR_COPY_FAIL));
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(UI_COL_BAD_TEXT), 0);
    return;
  }
  // Spools the kept list does not have, see doCopySpoolCreate().
  spoolCacheForget("spools created by copy");
  char reason[BATCH_REASON_MAX] = "";
  if (step != BS_DONE) runStopReason(step, made + 1, code, reason, sizeof(reason));
  noTagBatchFinish(ids, made, wanted, copyCardVendor(), reason);
  if (missing != CM_NONE && noTagYellowLineAllowed(ids[0], "copy parts not taken over", missing)) copyCreatedStatus(missing);
}

// Wurzel: the card's plan says how the spool's filament comes about: found
// (TFS_FOUND), or created from the database or the tag along with the spool.
// Asked the same for every spool of the run, a plan that creates one would
// make a filament per spool, five alike for five spools.
// Fix: a spool that made its filament (r.filament_id > 0) turns the plan into
// that filament, as the card does when only the filament was made: every
// further spool is created under it, and the backends create nothing else for
// a found filament (filamentForSpool() in backend_api.cpp; Bambuddy with its
// own inventory has no filament, reports none, and its plan stays as it is).
// Only the state and the id change: the weights stay the first spool's, so
// each spool is weighed against the same empty spool (tagCreateTare()).
// NOT-TO-DO: asking the plan again between two spools (runPlan()): it holds
// the loop once more per spool, and for a sort typed in it may come back with
// a twin to ask about in the middle of the run.
// See test_batch_flow.py (the run).
void batchTagCreate(const TagCreateInput& in, TagFilamentPlan* plan, const TagSpoolValues& values,
                    const char* vendor) {
  const int wanted = runWanted();
  int ids[BATCH_COUNT_MAX] = {};
  int made = 0, code = 0;
  bool price_lost = false;
  TagCreateResult r = {};
  BatchStep step = BS_NEXT;
  while (step == BS_NEXT) {
    runProgress(made + 1, wanted);
    code = serverReachNote(backendCreateFromTag(in, *plan, values, &r), true);
    const bool ok = (code == 200 || code == 201) && r.spool_id > 0;
    if (ok) {
      ids[made++] = r.spool_id;
      price_lost |= r.price_lost;
      if (r.filament_id > 0) { plan->state = TFS_FOUND; plan->filament_id = r.filament_id; }
    }
    logSDf("Batch: spool %d of %d: HTTP %d, spool %d, filament %d",
           made + (ok ? 0 : 1), wanted, code, r.spool_id, r.filament_id);
    step = batchCreateStep(made, wanted, ok, r.spool_sent && tagCreateAnswerLost(code));
  }
  loadingOverlayHide();
  if (made == 0) { tagCreateFailed(code, r); return; }
  spoolCacheForget("spools created from a tag");
  char reason[BATCH_REASON_MAX] = "";
  if (step != BS_DONE) runStopReason(step, made + 1, code, reason, sizeof(reason));
  // vendor is the card's input, which outlives the card (runCreate() reads it
  // after closing too).
  closeTagCreatePopup();
  noTagBatchFinish(ids, made, wanted, vendor, reason);
  if (price_lost && noTagYellowLineAllowed(ids[0], "price not saved", 0)) statusMessageShow(T(STR_TAGNEW_PRICE_LOST), UI_COL_WARN);
}

// ------------------------------------------------------------
//  After the run (batch_create.h)
// ------------------------------------------------------------

static int s_run_ids[BATCH_COUNT_MAX] = {};
static int s_run_n = 0;

void batchRunKeep(const int* ids, int n) {
  s_run_n = 0;
  if (!ids || n < 1) return;
  if (n > BATCH_COUNT_MAX) n = BATCH_COUNT_MAX;
  for (int i = 0; i < n; i++) s_run_ids[i] = ids[i];
  s_run_n = n;
}

int batchRunOf(int first_id, const int** ids) {
  if (ids) *ids = s_run_ids;
  return (first_id > 0 && s_run_n > 0 && s_run_ids[0] == first_id) ? s_run_n : 0;
}

// The spools from place `from` on were left without what `what` names for the
// log ("no label for"): the log has their numbers, and so does the status
// line in yellow (text_id, with a %s), unless the Bambu chip hint stands there.
static void runLeftOut(const int* ids, int from, int n, int text_id, const char* what) {
  char list[BATCH_IDS_TEXT_MAX];
  if (!batchIdsText(ids + from, n - from, list, sizeof(list))) list[0] = '\0';
  char lost[BATCH_IDS_TEXT_MAX + 24];
  snprintf(lost, sizeof(lost), "%s %s", what, list);
  logSDf("Batch: stopped, %s", lost);
  if (!noTagYellowLineAllowed(ids[0], lost, 0)) return;
  char msg[BATCH_IDS_TEXT_MAX + 32];
  snprintf(msg, sizeof(msg), T(text_id), list);
  statusMessageShow(msg, UI_COL_WARN);
}

// Wurzel: the picker writes the one spool it was opened for (the
// loc_patch_pending branch of more_info_screen.cpp), and a run has several:
// asked once per spool, the same shelf would be chosen five times.
// Fix: the picker is asked for the first spool only, and what it wrote
// (locationPickerChosen()) goes to each of the others here, one call each
// like the picker's own. The first failure stops: every call after it would
// wait out the same timeout on a server that is gone, and the spools left
// without are named once.
// NOT-TO-DO: the picker writing every spool of the run itself: it is the
// original's and serves the pad, the AMS bays and "More info" as well.
// See test_batch_flow.py (after the run).
void batchLocationToRest(int first_id, const char* name) {
  const int* ids = nullptr;
  const int n = batchRunOf(first_id, &ids);
  int at = 1;
  for (; at < n; at++) {
    char buf[48];
    snprintf(buf, sizeof(buf), T(STR_BATCH_LOC_PROGRESS), at + 1, n);
    loadingOverlayShow(buf);
    const int code = serverReachNote(backendPatchSpoolLocation(cfg_spoolman_base, ids[at], name, 8000), true);
    logSDf("Batch: location of spool %d (%d of %d) -> '%s', HTTP %d", ids[at], at + 1, n, name ? name : "", code);
    if (!backendWriteOk(code)) break;
  }
  loadingOverlayHide();
  if (at >= n) return;
  runLeftOut(ids, at, n, STR_BATCH_LOC_MISSING, "no location on");
}

// The labels of a run, one after another.
//   LR_SHEET  "Print" with the preview on: the sheet up next is the first's
//   LR_RUN    printing; s_lr_next is the place of the next label
enum LabelRun : uint8_t { LR_IDLE = 0, LR_SHEET, LR_RUN };

static LabelRun       s_lr       = LR_IDLE;
static int            s_lr_first = 0;                  // the run's first spool
static int            s_lr_next  = 0;
static LabelKind      s_lr_kind  = LABEL_KIND_SPOOL;
static SpoolLabelData s_lr_data{};                     // the first's, taken at the start

static void labelsIdle() {
  s_lr = LR_IDLE;
  printCardSetStep(0, 0);
}

// The sheet's own print, as printerLastLabelResult() keeps its verdict: the
// result's text. Printed and sent each have a text of their own
// (labelPrintResultString()), so the text says which it was.
static bool labelWentOut(int text) {
  return text == labelPrintResultString(LP_OK) || text == labelPrintResultString(LP_NET_SENT);
}

// Wurzel: with the preview on, the sheet prints the first label itself, and
// every way out of it but its print button means "not now": back and the
// printer row pass through the sheet's own branches, the X and any other
// navigation through showMainScreen().
// Fix: the sheet's print goes on with the rest (batchLabelsStart(kind, 1)),
// its two own ways out drop them (batchLabelsForget()), and the tick drops
// them once the sheet is gone without either.
// NOT-TO-DO: dropping them in closePrintSheet(): the print button closes the
// sheet before it prints. Nor counting the wait for the sheet as busy
// (batchLabelsBusy()): a sheet a navigation closed would hold the questions
// after a lift for good.
void batchLabelsAfterSheet(int first_id) {
  const int n = batchRunOf(first_id, nullptr);
  if (n < 2) return;
  s_lr       = LR_SHEET;
  s_lr_first = first_id;
  // The sheet's print is the run's first label: its card says so as well.
  printCardSetStep(1, n);
  logSDf("Batch: %d labels, the print sheet first for spool %d", n, first_id);
}

void batchLabelsStart(LabelKind kind, int from) {
  const bool sheet = from > 0;
  if (sheet && s_lr != LR_SHEET) return;   // a print from the sheet that is no run's
  const int sheet_for = s_lr_first;
  labelsIdle();
  SpoolLabelData d{};
  const int* ids = nullptr;
  const int n = labelSpoolFromScan(&d) ? batchRunOf(d.id, &ids) : 0;
  if (n < 2 || from >= n || (sheet && d.id != sheet_for)) {
    logSDf("Batch: labels not started, spool %d on the screen heads no run (%d)", d.id, n);
    return;
  }
  if (sheet && !labelWentOut(printerLastLabelResult())) {
    runLeftOut(ids, 0, n, STR_BATCH_LABELS_MISSING, "no label for");
    return;
  }
  s_lr_data  = d;
  s_lr_first = d.id;
  s_lr_kind  = kind;
  s_lr_next  = from;
  s_lr       = LR_RUN;
  logSDf("Batch: labels %d to %d of the run of spool %d", from + 1, n, d.id);
}

// Wurzel: there is no print queue, a print holds the loop while it runs, and
// its card stays up after it: 4 s for a printed label, 2 s for a Brother's
// "sent", until OK for a failure. The next label printed under a card still
// up would put two answers on one card; all of them in one pass would hold
// the loop for every label.
// Fix: one label per pass at most, from the loop, and only once the card of
// the one before has closed (printCardOpen()). The Brother's held line is
// closed by the next print itself (printAttemptStart() in printer_screen.cpp),
// as for two prints by hand. Each label is the first spool's data taken at
// the start with its own number; the dates and the QR code follow the number
// in the print. A result other than printed or sent stops the rest: the next
// would meet the same printer, and a card per label waiting for OK would ask
// the same thing again and again.
// NOT-TO-DO: reading the screen again for each label: a tag laid on during
// the run takes the screen, and the labels left would carry its spool.
// See test_batch_flow.py (after the run).
void batchLabelsTick() {
  if (s_lr == LR_SHEET) {
    if (!show_print_sheet_pending && !printSheetOpen()) {
      logSDf("Batch: the print sheet for spool %d closed without its print, no labels for the rest", s_lr_first);
      labelsIdle();
    }
    return;
  }
  if (s_lr != LR_RUN || printCardOpen()) return;
  const int* ids = nullptr;
  const int n = batchRunOf(s_lr_first, &ids);
  if (s_lr_next >= n) { labelsIdle(); return; }
  s_lr_data.id = ids[s_lr_next];
  printCardSetStep(s_lr_next + 1, n);
  const LabelPrintResult r = printerPrintSpoolData(s_lr_data, s_lr_kind);
  printCardSetStep(0, 0);
  logSDf("Batch: label %d of %d, spool %d, result %d", s_lr_next + 1, n, s_lr_data.id, (int)r);
  if (r != LP_OK && r != LP_NET_SENT) {
    labelsIdle();
    runLeftOut(ids, s_lr_next, n, STR_BATCH_LABELS_MISSING, "no label for");
    return;
  }
  if (++s_lr_next >= n) {
    labelsIdle();
    logSDf("Batch: all %d labels of the run of spool %d out", n, s_lr_first);
  }
}

void batchLabelsForget() {
  if (s_lr != LR_IDLE) logSDf("Batch: the labels of the run of spool %d dropped", s_lr_first);
  labelsIdle();
}

bool batchLabelsBusy() { return s_lr == LR_RUN; }
