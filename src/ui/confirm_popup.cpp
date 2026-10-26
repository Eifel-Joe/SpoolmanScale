#include "confirm_popup.h"
#include "ui_common.h"
#include <math.h>
#include "app/app_state.h"
#include "services/backend.h"
#include "services/backend_api.h"

#include <Arduino.h>
#include <lvgl.h>
#include <cstring>

#include "hardware/sd_logger.h"
#include "services/ams_assign.h"
#include "services/ams_pick.h"
#include "services/auto_weight_state.h"
#include "services/prefs_store.h"
#include "services/spool_extras.h"
#include "services/spool_types.h"
#include "services/spoolman_actions.h"
#include "services/tag_write.h"
#include "dried_action.h"
#include "more_info_screen.h"
#include "app/deferred_actions.h"
#include "app/id_spool.h"
#include "spoolman_screen.h"
#include "tare_entry.h"
#include "extras_chooser.h"
#include "weight_popup.h"
#include "lang.h"
#include "theme.h"


static lv_obj_t *confirm_popup = nullptr;
static int confirm_action = 0;
// The dialog this file opens besides the question itself (where the empty
// spool weight goes is tare_scope_popup.cpp's). It used to be a local of the
// callback that built it, so nothing could close it from outside and a
// navigation left it standing over the next screen.
static lv_obj_t *s_cap_popup   = nullptr;   // BamBuddy's label weight question

void closeConfirmPopups() {
  closeConfirmPopup();
  closeWeightPopups();
  closeSpoolWeightScope();
  releaseScreen(&s_cap_popup);
  closeExtrasChooser();
  closeTareEntry();
}

// The weight popup counts as one of these, and with it the parts chooser
// opened from it (isWeightPopupOpen()). It lived in this file until
// 26.10.2026, and every place that waits for a question to be answered -
// the automatic weighing, the tag lookups, uiModalWaiting() - asks here.
bool isConfirmPopupOpen() {
  return confirm_popup != nullptr || isWeightPopupOpen();
}

void closeConfirmPopup() {
  // Deleted on the next timer pass, not here: every caller of this is one of
  // the popup's own buttons, and deleting a button's ancestor from inside its
  // callback is the one thing LVGL's event dispatch cannot be trusted with.
  // The pointer is dropped now, so the popup counts as closed from this line.
  if (confirm_popup) { lv_obj_del_async(confirm_popup); confirm_popup = nullptr; }
  confirm_action = 0;
}

// What a button asked for, carried to appLoop(). Every one of these reaches
// the backend, and they used to run inside the button's own callback with
// the touch panel dead for the length of the request. One slot is enough:
// the popups are modal, a second press cannot land before the first has run.
enum ConfirmJob : uint8_t {
  CJ_NONE = 0,
  CJ_WEIGHT,          // the remaining weight, plus the AMS notes
  CJ_INITIAL,         // a new spool's initial weight
  CJ_TARE_SPOOL,      // the empty spool weight, on this spool
  CJ_TARE_FILAMENT,   // ... on the filament
  CJ_TARE_VENDOR,     // ... on the brand
  CJ_ARCHIVE,
  CJ_CAP_RAISE,       // BamBuddy: raise the label weight, then write
  CJ_CAP_KEEP,        // BamBuddy: keep the label, write anyway
  CJ_FULL_SPOOL       // a full spool: the tare, then label and rest (design L14)
};
static ConfirmJob s_job       = CJ_NONE;
static float      s_job_value = 0.0f;
// The pad reading the AMS note wants; for the full spool the bare reading
// the fill was taken with.
static float      s_job_gross = 0.0f;
static float      s_job_fill  = 0.0f;   // the full spool: its filament, grams
// The spool an empty spool weight was meant for. Every tare write goes to
// whatever spool is loaded when it runs, and a spool swapped while the scope
// question stood would take a tare that belongs to the one before it.
static int        s_job_spool_id  = 0;  // carried with the parked write

static void tareFollowUp();
static void tareDropFollowUp();
static void askFreeTag();
// The question follows the archive a pass later: the popup that asked for
// the archive is deleted at the end of its pass, and the pool holds one
// question at a time.
static bool s_ask_free_tag_pending = false;

void handleConfirmPopupDeferredActions() {
  if (s_ask_free_tag_pending) {
    s_ask_free_tag_pending = false;
    askFreeTag();
  }
  if (s_job == CJ_NONE) return;
  const ConfirmJob job = s_job;
  s_job = CJ_NONE;
  const bool tare_job = job == CJ_TARE_SPOOL || job == CJ_TARE_FILAMENT ||
                        job == CJ_TARE_VENDOR || job == CJ_FULL_SPOOL;
  if (tare_job && sm_id != s_job_spool_id) {
    logSDf("Tare: %.1f g was for spool %d, spool %d is loaded now - not stored",
           s_job_value, s_job_spool_id, sm_id);
    tareDropFollowUp();   // its initial weight would follow the same tare
    return;
  }
  switch (job) {
    case CJ_WEIGHT:
      patchSpoolmanWeight(s_job_value);
      // Remembered so the question can come up on removal. A yes there
      // reports the same weight once more, which is the only way to open the
      // window. The cap check cannot interfere: it is BamBuddy only and
      // amsAskActive() requires FilaMan.
      if (amsAskActive()) amsNoteMeasurement(sm_id, s_job_value, s_job_gross, true);
      if (amsPickActive()) amsPickNote(sm_id, sm_filament_name);
      break;
    case CJ_INITIAL:
      patchInitialWeight(s_job_value);
      break;
    // The initial weight follows a tare only once the server took it: after a
    // failed one it would rest on a tare the server does not have.
    case CJ_TARE_SPOOL:
      if (patchSpoolWeight(s_job_value)) tareFollowUp(); else tareDropFollowUp();
      break;
    case CJ_TARE_FILAMENT:
      if (patchFilamentSpoolWeight(s_job_value)) tareFollowUp(); else tareDropFollowUp();
      break;
    case CJ_TARE_VENDOR:
      if (patchVendorSpoolWeight(s_job_value)) tareFollowUp(); else tareDropFollowUp();
      break;
    case CJ_FULL_SPOOL: {
      // Design L14, in this order and each step only after the one before
      // it: the tare (a picked type wins over the computed one, s_job_value
      // is what was picked), the label weight, the rest. The server keeps
      // whole grams and takes the rest as the reading less the tare it holds,
      // so the three are worked out in whole grams, the rest never above the
      // label (spoolTypeFullSpoolWrite). A rest above the fill lifts the label
      // to it, the proxy turns down a negative use.
      // The label is written only where it changes: the write sets the label
      // and clears the use in one (patchInitialWeight), so repeating the label
      // the spool already has would only open a window in which a failed rest
      // leaves a used spool reading full. The rest alone is then enough.
      // NOT-TO-DO: no skip on "the rest is below the label" alone: a typed
      // fill that differs from sm_total (full spool) is a new label and is
      // written as before.
      const FullSpoolWrite w = spoolTypeFullSpoolWrite(s_job_gross, s_job_fill, s_job_value);
      if (!patchSpoolWeight((float)w.tare_g)) {
        logSD("Full spool: tare not stored, nothing else written");
        break;
      }
      logSDf("Full spool: %.0f g filament, %.1f g bare, %d g tare: label %d g, rest %.1f g",
             s_job_fill, s_job_gross, w.tare_g, w.label_g, w.rest_g);
      const int label_have_g = spoolTypeGrams(sm_total);
      if (w.label_g != label_have_g) {
        logSDf("Full spool: label set to %d g (the spool had %d g)", w.label_g, label_have_g);
        if (!patchInitialWeight((float)w.label_g)) break;
      } else {
        logSDf("Full spool: label unchanged at %d g", w.label_g);
      }
      // Past the label check: the label holds this rest, set now or before.
      patchSpoolmanWeight(w.rest_g, true);
      break;
    }
    case CJ_ARCHIVE:
      if (patchArchiveSpool()) s_ask_free_tag_pending = true;
      break;
    case CJ_CAP_RAISE: {
      // Order matters: the label first, so the weight that follows has room.
      // In BamBuddy's own inventory patchInitialWeight writes label and
      // consumption together, which already leaves the spool at full; the
      // weight write after it sets the rest behind Spoolman and stamps
      // last_weighed_at and last_scale_weight. A label the server refused
      // leaves no room, so nothing follows it, as for a full spool. The label
      // goes over in whole grams and the rest is cut to it, so it cannot
      // stand above the label (spoolTypeFullSpoolWrite does the same).
      const float label = (float)spoolTypeGrams(s_job_value);
      if (!patchInitialWeight(label)) {
        logSDf("Label raise to %.0f g refused, weight not written", label);
        break;
      }
      patchSpoolmanWeight(s_job_value < label ? s_job_value : label, true);
      break;
    }
    case CJ_CAP_KEEP:
      // Written anyway, so the scale and the server agree that a weighing
      // happened. BamBuddy clamps it, which is what the user just accepted.
      patchSpoolmanWeight(s_job_value, true);
      break;
    default: break;
  }
}


// The spool "release the tag" was asked for. The answer comes a touch later,
// and a spool lifted off meanwhile clears sm_id, so the unlink is dropped
// rather than sent to whatever lies there then.
static int s_free_tag_spool_id = 0;
// The tag the erase line was about, as the cache spelt it; empty when the
// question had no erase line.
static char s_free_tag_uid[26] = "";

// Asked once the archive is through, never before: a failed archive must not
// leave a spool that is still active without its tag. Only for a spool that
// has a binding, and the erase line only when the tag on the reader is one
// requestTagEraseNow() would actually erase.
static void askFreeTag() {
  if (!sm_found || sm_id <= 0) return;
  if (smBoundUidCount() == 0 && !tag_present) return;
  s_free_tag_spool_id = sm_id;
  char msg[224];
  const bool erase = tagIsWritableNtag() && tagCachedHasRecord();
  snprintf(s_free_tag_uid, sizeof(s_free_tag_uid), "%s", erase ? tagCachedUid() : "");
  snprintf(msg, sizeof(msg), erase ? "%s\n\n%s" : "%s", T(STR_FREE_TAG_ASK),
           T(STR_FREE_TAG_ERASE_LINE));
  logSDf("Archive: asking to release the tag of spool %d (erase=%d)", sm_id, erase ? 1 : 0);
  showConfirmPopup(msg, 7);
}

static void freeTagConfirmed() {
  if (sm_id != s_free_tag_spool_id || sm_id <= 0) {
    logSDf("Release tag: was for spool %d, spool %d is loaded now - not sent",
           s_free_tag_spool_id, sm_id);
    return;
  }
  // Every tag of the spool, the other flange too: an archived spool keeps
  // none. Only the one on the reader can be erased, the other keeps its data.
  requestSpoolUnlink(s_free_tag_spool_id, true, true, s_free_tag_uid);
}

static float s_cap_measured  = 0.0f;
static bool  s_tare_then_new = false;

// After a tare is stored from the New spool flow the initial weight follows
// from it: what is left of the reading once the spool itself is taken off.
// Done here rather than in the caller so the two writes stay in the order that
// makes the second one correct. Only called once the server took the tare.
static void tareFollowUp() {
  if (!s_tare_then_new) return;
  s_tare_then_new = false;
  // The tare was derived as reading minus nominal, so what is left of the
  // reading once the spool comes off is the nominal weight itself. Reading
  // scale_weight_g again here would use a value that has moved on since the
  // popup opened - the pad drifts, someone leans on the bench - and leave the
  // tare and the initial weight contradicting each other, which is the exact
  // thing deriving the tare is meant to prevent.
  patchInitialWeight(sm_total);
}

static void tareDropFollowUp() {
  s_tare_then_new = false;
}

// The buttons of the scope question (tare_scope_popup.cpp) cannot reach the
// job slot, so they park the write through here. The flag for the follow-up
// is set with the job and not when the question opens: a question closed
// without an answer leaves nothing behind that the next tare could pick up.
void confirmParkTare(TareScope scope, float grams, int spool_id, bool then_new_spool) {
  switch (scope) {
    case TARE_SCOPE_FILAMENT: s_job = CJ_TARE_FILAMENT; break;
    case TARE_SCOPE_VENDOR:   s_job = CJ_TARE_VENDOR;   break;
    default:                  s_job = CJ_TARE_SPOOL;    break;
  }
  s_job_value     = grams;
  s_job_spool_id  = spool_id;
  s_tare_then_new = then_new_spool;
}

// The full spool of the empty spool button (tare_entry.cpp), parked like a
// tare: the list it was picked from answers in the loop, after this file's
// jobs ran, so it runs on the next pass.
void confirmParkFullSpool(float tare, float fill, float bare, int spool_id) {
  s_job           = CJ_FULL_SPOOL;
  s_job_value     = tare;
  s_job_fill      = fill;
  s_job_gross     = bare;
  s_job_spool_id  = spool_id;
  s_tare_then_new = false;   // its own follow-up, not the New spool one
}

// Whether New spool has to derive the tare from this reading because nothing
// else is known, and what it would store.
//
// Only when no tare exists at any level. The two numbers in the reading are
// the spool and the filament, and exactly one of them can be measured
// independently: the spool. Its weight is a property of the spool MODEL and
// barely varies between spools of the same kind, while what a manufacturer
// actually winds onto an individual spool varies by a couple of percent. So a
// tare that exists - measured here, or inherited from the filament or the
// brand - is the more trustworthy of the two, and the filament weight is what
// gets measured against it.
//
// Deriving on top of a known tare would do the opposite: it folds this one
// spool's fill tolerance into a tare that other spools inherit, where a few
// grams of error stop being local and start applying to every spool of that
// filament or that brand.
//
// This is the only moment a tare can be had at all without emptying the spool
// first, which is why it stays for the case where there is nothing.
//
// Asked in two places - the button label and its handler - so that the label
// can never promise a different number than the press writes.
static bool newSpoolDerivesTare(float* out_tare) {
  if (sm_tare_source != TARE_NONE) return false;
  const float derived = spoolExtrasBareReading() - sm_total;
  const bool plausible = (sm_total > 0.0f) &&
                         (derived >= NEW_SPOOL_TARE_MIN_G) &&
                         (derived <= NEW_SPOOL_TARE_MAX_G);
  if (!plausible) return false;
  if (out_tare) *out_tare = derived;
  return true;
}

void confirmQueueWeight(float netto_g, float gross_g) {
  // The numbers are taken now, the request runs on the next loop pass.
  s_job = CJ_WEIGHT; s_job_value = netto_g; s_job_gross = gross_g;
}

// Where nothing is known about the empty spool, taking it as full derives
// that first and asks where it goes; the initial weight follows from it.
void confirmNewSpool(float netto_g) {
  float derived = 0.0f;
  if (newSpoolDerivesTare(&derived)) {
    showSpoolWeightScope(derived, true, true);   // stores the tare, then the initial
    return;
  }
  s_job = CJ_INITIAL; s_job_value = netto_g;
}

// Name the weight the button will really write. Where the tare is derived
// that is the nominal filament weight, not the reading minus a tare that is
// about to be replaced - and those two differ by the whole spool.
float confirmNewSpoolShownG(float netto_g) {
  return newSpoolDerivesTare(nullptr) ? sm_total : netto_g;
}

void showConfirmPopup(const char* msg, int action) {
  closeConfirmPopup();
  confirm_action = action;

  confirm_popup = lv_obj_create(lv_scr_act());
  lv_obj_set_size(confirm_popup, 480, 320);
  lv_obj_set_pos(confirm_popup, 0, 0);
  lv_obj_set_style_bg_color(confirm_popup, lv_color_hex(UI_COL_SCRIM), 0);
  lv_obj_set_style_bg_opa(confirm_popup, UI_OPA_SCRIM, 0);
  lv_obj_set_style_border_width(confirm_popup, 0, 0);
  lv_obj_set_style_radius(confirm_popup, 0, 0);
  lv_obj_set_style_pad_all(confirm_popup, 0, 0);  // KRITISCH: kein Default-Padding!
  lv_obj_clear_flag(confirm_popup, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *box = lv_obj_create(confirm_popup);
  lv_obj_set_style_bg_color(box, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_border_color(box, lv_color_hex(UI_COL_POPUP_BORDER), 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_radius(box, 12, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl_q = lv_label_create(box);
  lv_label_set_text(lbl_q, msg);
  lv_obj_set_style_text_color(lbl_q, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_style_text_font(lbl_q, &lv_font_montserrat_ext_16, 0);
  lv_obj_set_style_text_align(lbl_q, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lbl_q, LV_LABEL_LONG_WRAP);

  // Standard popup: an icon, the question, yes / no.
  //
  // The icon names the consequence before the sentence is read, and the
  // three that land here are different enough to deserve their own: a drop
  // for drying, a bin for archiving, a warning for throwing an address
  // away. The drop is the same one the BamBuddy options screen already uses
  // for the drying date.
  //
  // Height follows the text. Two short lines at 20 px need far less room
  // than the address warning, which is a full paragraph and stays at 16 px
  // so it does not run into the buttons.
  const char*      icon     = LV_SYMBOL_WARNING;
  uint32_t         icon_col = UI_COL_WARN;
  const lv_font_t* q_font   = &lv_font_montserrat_ext_20;
  int              box_h    = 210;
  if (action == 1) {                       // dried today
    icon = LV_SYMBOL_TINT;  icon_col = UI_COL_DRY;
  } else if (action == 3) {                // archive spool
    icon = LV_SYMBOL_TRASH; icon_col = UI_COL_ARCHIVE_TEXT;
  } else {                                 // long text, e.g. discard address
    q_font = &lv_font_montserrat_ext_16;
    box_h  = 250;
  }
  const int BOX_W = 400;
  const int BTN_H = 56;
  const int BTN_Y = box_h - BTN_H - 18;

  lv_obj_set_size(box, BOX_W, box_h);
  lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_pad_all(box, 0, 0);

  lv_obj_t *lbl_icon = lv_label_create(box);
  lv_label_set_text(lbl_icon, icon);
  lv_obj_set_style_text_color(lbl_icon, lv_color_hex(icon_col), 0);
  // 24 rather than something larger on purpose: it is the biggest size the
  // firmware already links. Pulling in a font for one glyph cost 74 kB of
  // flash when this was first written at 32.
  lv_obj_set_style_text_font(lbl_icon, &lv_font_montserrat_ext_24, 0);
  lv_obj_align(lbl_icon, LV_ALIGN_TOP_MID, 0, 14);

  lv_obj_set_style_text_font(lbl_q, q_font, 0);
  lv_obj_set_width(lbl_q, BOX_W - 40);
  lv_obj_align(lbl_q, LV_ALIGN_TOP_MID, 0, 62);

  lv_obj_t *btn_ja = lv_btn_create(box);
  lv_obj_set_size(btn_ja, 170, BTN_H);
  lv_obj_set_pos(btn_ja, 12, BTN_Y);
  lv_obj_set_style_bg_color(btn_ja, lv_color_hex(UI_COL_OK_BG), 0);
  lv_obj_set_style_bg_color(btn_ja, lv_color_hex(UI_COL_OK_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_ja, 8, 0);
  lv_obj_set_style_shadow_width(btn_ja, 0, 0);
  lv_obj_add_event_cb(btn_ja, [](lv_event_t *e) {
    int act = confirm_action;
    closeConfirmPopup();
    if (act == 1) btn_dried_cb(nullptr);
    if (act == 3) s_job = CJ_ARCHIVE;
    // 7 = release the tag of the spool just archived. Flags only.
    if (act == 7) freeTagConfirmed();
    if (act == 4) spoolmanClearHost();
    // 5 = downgrade confirmed on the OTA screen. Only the flag here; the
    // download runs from appLoop().
    if (act == 5) gh_downgrade_pending = true;
    // 6 = reset the calibration. The flag again: it writes NVS and rebuilds
    // the screen this button was opened from.
    if (act == 6) cal_reset_pending = true;
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_ja = lv_label_create(btn_ja);
  lv_label_set_text(lbl_ja, T(action == 7 ? STR_BTN_FREE_TAG : STR_BTN_CONFIRMED));
  lv_obj_set_style_text_font(lbl_ja, &lv_font_montserrat_ext_18, 0);
  lv_obj_set_style_text_color(lbl_ja, lv_color_hex(UI_COL_OK_TEXT), 0);
  lv_obj_center(lbl_ja);

  lv_obj_t *btn_nein = lv_btn_create(box);
  lv_obj_set_size(btn_nein, 170, BTN_H);
  lv_obj_set_pos(btn_nein, 218, BTN_Y);
  lv_obj_set_style_bg_color(btn_nein, lv_color_hex(UI_COL_BAD_BG), 0);
  lv_obj_set_style_bg_color(btn_nein, lv_color_hex(UI_COL_BAD_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_nein, 8, 0);
  lv_obj_set_style_shadow_width(btn_nein, 0, 0);
  lv_obj_add_event_cb(btn_nein, [](lv_event_t *e){ closeConfirmPopup(); }, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_nein = lv_label_create(btn_nein);
  // Keeping the tag is a fine answer after an archive, not a cancel.
  lv_label_set_text(lbl_nein, T(action == 7 ? STR_TW_BTN_KEEP : STR_CANCEL));
  lv_obj_set_style_text_font(lbl_nein, &lv_font_montserrat_ext_18, 0);
  lv_obj_set_style_text_color(lbl_nein, lv_color_hex(UI_COL_BAD_TEXT), 0);
  lv_obj_center(lbl_nein);
}

// ============================================================
//  BAMBUDDY: MEASUREMENT ABOVE THE LABEL WEIGHT
//
//  BamBuddy's built-in inventory keeps the consumed grams and derives the
//  rest, so a spool holding more than its label says cannot be stored - the
//  remainder clamps back to full. Behind Spoolman BamBuddy clamps the rest
//  to the label as well (spoolLabelAsk). Raising the label is the only way
//  to keep the number, and that changes a field the user owns, so it is
//  asked.
// ============================================================
void showBamBuddyCapPopup(float measured_g, float label_g) {
  releaseScreen(&s_cap_popup);
  lv_obj_t *popup = lv_obj_create(lv_scr_act());
  s_cap_popup = popup;
  lv_obj_set_size(popup, 480, 320);
  lv_obj_set_pos(popup, 0, 0);
  lv_obj_set_style_bg_color(popup, lv_color_hex(UI_COL_GROUND), 0);
  lv_obj_set_style_border_width(popup, 0, 0);
  lv_obj_set_style_radius(popup, 0, 0);
  lv_obj_set_style_pad_all(popup, 0, 0);
  lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(popup);
  char title_buf[40];
  copyT(title_buf, sizeof(title_buf), STR_BB_CAP_TITLE);
  lv_label_set_text(title, title_buf);
  lv_obj_set_style_text_color(title, lv_color_hex(UI_COL_WARN), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_ext_18, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

  lv_obj_t *body = lv_label_create(popup);
  char body_buf[240];
  snprintf(body_buf, sizeof(body_buf), T(STR_BB_CAP_BODY), measured_g, label_g);
  lv_label_set_text(body, body_buf);
  lv_obj_set_style_text_color(body, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_style_text_font(body, &lv_font_montserrat_ext_14, 0);
  lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(body, 440);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 52);

  // Remembered for the two handlers, which cannot capture anything.
  s_cap_measured = measured_g;

  lv_obj_t *b_raise = lv_btn_create(popup);
  lv_obj_set_size(b_raise, 440, 56); lv_obj_set_pos(b_raise, 20, 150);
  lv_obj_set_style_bg_color(b_raise, lv_color_hex(UI_COL_PICKED_BG), 0);
  lv_obj_set_style_radius(b_raise, 8, 0);
  lv_obj_set_style_shadow_width(b_raise, 0, 0);
  { lv_obj_t *l = lv_label_create(b_raise);
    char buf[48];
    snprintf(buf, sizeof(buf), T(STR_BB_CAP_RAISE), measured_g);
    lv_label_set_text(l, buf);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_OK_TEXT_2), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_16, 0);
    lv_obj_center(l); }
  lv_obj_add_event_cb(b_raise, [](lv_event_t *e) {
    s_job = CJ_CAP_RAISE; s_job_value = s_cap_measured;
    releaseScreen(&s_cap_popup);
  }, LV_EVENT_CLICKED, NULL);

  lv_obj_t *b_keep = lv_btn_create(popup);
  lv_obj_set_size(b_keep, 440, 48); lv_obj_set_pos(b_keep, 20, 216);
  lv_obj_set_style_bg_color(b_keep, lv_color_hex(UI_COL_LINE_SOFT), 0);
  lv_obj_set_style_radius(b_keep, 8, 0);
  lv_obj_set_style_shadow_width(b_keep, 0, 0);
  { lv_obj_t *l = lv_label_create(b_keep);
    char buf[40];
    copyT(buf, sizeof(buf), STR_BB_CAP_KEEP);
    lv_label_set_text(l, buf);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_INK_2), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_16, 0);
    lv_obj_center(l); }
  lv_obj_add_event_cb(b_keep, [](lv_event_t *e) {
    s_job = CJ_CAP_KEEP; s_job_value = s_cap_measured;
    releaseScreen(&s_cap_popup);
  }, LV_EVENT_CLICKED, NULL);
}
