#include "ui/no_tag_create.h"

#include <lvgl.h>
#include <stdio.h>

#include "app/app_loop.h"
#include "app/app_state.h"
#include "app/deferred_actions.h"
#include "app/id_spool.h"
#include "hardware/sd_logger.h"
#include "services/label_printer.h"
#include "services/location_state.h"
#include "services/no_tag_create_rules.h"
#include "ui/main_screen_helpers.h"
#include "ui/more_info_screen.h"
#include "ui/navigation.h"
#include "ui/spool_flow.h"
#include "ui/spool_flow_internal.h"
#include "ui/theme.h"
#include "ui/ui_common.h"
// Last, see the note on ArduinoJson and T() in tag_create_popup.cpp.
#include "lang.h"

static bool s_no_tag = false;

void noTagModeSet(bool on) {
  if (on == s_no_tag) return;
  s_no_tag = on;
  // The reason is fixed by the value: on has one way in, off has several (a
  // tag on the pad, another entry, the end of a create) and the SHOW and
  // BTN lines before this one name which.
  logSD(on ? "NoTag: mode on (no tag and no id on the pad)"
           : "NoTag: mode off (another way in, or the create is over)");
}

bool noTagMode() {
  return s_no_tag;
}

// Wurzel: a lifted tag leaves its ids (g_tag, link_tag_uid) until the 60 s timer
// or the clear button empties the display, but the button already reads
// "Create spool" the moment the tag is gone. The choice would then decide the
// flag from those ids (off, "have an id"), show the lifted tag's UID and link
// the new spool to a tag that is no longer there.
// Fix: on an empty screen with leftovers, drop them first, the way the timer
// does (dropLiftedTagDisplay() also ends the timer's wait), then open the
// choice. In any other state this only opens it, as before.
// NOT-TO-DO: clearing inside showCopyEntryPopup(): it is also the way back from
// the database and the sort entry, with a tag possibly on the pad again.
// See test_no_tag_entry.py.
void noTagEntryTap() {
  if (!tag_present && !sm_found &&
      (g_tag.uid_str[0] || g_tag.tray_uuid[0] || link_tag_uid[0])) {
    logSD("NoTag: create spool drops the leftovers of a lifted tag");
    dropLiftedTagDisplay();
  }
  showCopyEntryPopup();
}

const char* noTagEntryTitle() {
  return T(s_no_tag ? STR_BTN_CREATE_SPOOL : STR_BTN_COPY_SPOOL);
}

// ------------------------------------------------------------
//  The end of a create
// ------------------------------------------------------------

// After the create: the location picker, then the question for a label. The
// ending below only arms the picker; the picker, the card and its answers
// follow the ending.
enum LabelAnswer : uint8_t { LA_NONE = 0, LA_PRINT, LA_NO };

static lv_obj_t*   s_card     = nullptr;    // the scrim with the box on it
static int         s_card_for = 0;          // the spool the card asks about
static LabelAnswer s_answer   = LA_NONE;    // parked by a button, taken by the tick
static int         s_ask_for  = 0;          // armed when the picker ended: ask for this spool, 0: none
static bool        s_ask_wait = false;      // the card is built a pass after it was armed
static int         s_loc_for  = 0;          // armed by the ending: the picker for this spool, 0: none
static bool        s_loc_wait = false;      // the picker is asked for a pass after it was armed
static bool        s_chip_hint = false;     // the status line holds the Bambu chip hint

// Whether the spool is the one on the screen, by its number and with no tag
// on the pad: the only state in which a label printed "for the screen" is
// this spool's.
static bool shownStill(int spool_id) {
  return spool_id > 0 && !tag_present && sm_found && sm_id == spool_id;
}

// Wurzel, no link: finishCopyFlow() binds the new spool to the tag on the pad
// (g_tag.tray_uuid, else link_tag_uid). A create without a tag has none: both
// are empty, or they are what a lifted tag left behind, or they belong to a
// tag laid on while the card was up. The old ending would link the new spool
// to whatever they hold, or say "no tag" in red about a spool that was made.
// Fix: nothing is linked. The new number is shown the way "spool by ID" shows
// it (idSpoolAdopt()), so weighing, location, drying, AMS and labels work on
// it at once; the tag is stuck on later and linked the usual way.
// NOT-TO-DO: linking the tag that happens to lie on the pad when the create
// ends: a roll in its box carries a label that may well be another spool's.
// A tag laid on meanwhile is a lookup of its own, and idSpoolAdopt() leaves
// the screen to it (the question for a label is then not asked either).
//
// Wurzel, the first weighing: both endings mark a spool whose type was asked
// on the way (tareEntryFirstWeighDone()), so that its first weighing does not
// ask again. Here the type was chosen without weighing anything: it is an
// assumption about a roll nobody measured.
// Fix: no mark. The first weighing with a tag on it checks the type (a match
// within 5 g is no question, a mismatch suggests the one that fits), which is
// what the spec asks of it (V35).
// NOT-TO-DO: marking it "asked" because the list was answered: the type would
// then never be checked against a measurement.
// See test_no_tag_finish.py.
void noTagCreateFinish(int new_spool_id, const char* vendor) {
  linkPickerClose();
  showMainScreen();
  idSpoolAdopt(new_spool_id);
  const bool bambu = noTagCreatedNote(vendor) == NTN_BAMBU_CHIP;
  char msg[96];
  snprintf(msg, sizeof(msg), T(bambu ? STR_NOTAG_CREATED_BAMBU : STR_NOTAG_CREATED), new_spool_id);
  statusMessageShow(msg, UI_COL_GOOD);
  s_chip_hint = bambu;   // see noTagYellowLineAllowed()
  noTagModeSet(false);
  // Only when the new spool is on the screen after all: idSpoolAdopt() does
  // not take it over while a tag is on the pad, and it may not have loaded.
  // Then neither the picker nor the question for a label comes.
  s_loc_for  = (sm_found && sm_id == new_spool_id) ? new_spool_id : 0;
  s_loc_wait = s_loc_for != 0;
  logSDf("NoTag: spool %d created, shown by its number (%s), location picker %s",
         new_spool_id, bambu ? "Bambu note" : "plain note", s_loc_for ? "armed" : "not asked");
}

// Wurzel: the status line holds one text and has no queue, and both callers
// wrote a yellow line over the green one after the finish (the parts a copy
// left behind, a price the server did not keep). For a Bambu Lab spool that
// replaced the chip hint, the one line that keeps Bambuddy from making the
// roll a second time when it goes unknown into the first AMS (spec 5.3 X);
// with no tag on the pad nothing paints the line again, so it never came back.
// Fix: while the chip hint stands (s_chip_hint, set by the finish) it is the
// last word: the callers ask here before they write a yellow line, a refusal
// puts what was lost in the log with the spool's number instead. With the
// plain note the yellow line still replaces the green one, as it always did.
// NOT-TO-DO: one line with both: the status line has 292 px (262 in the
// narrow header) and the hint alone takes 236 to 259. One after the other:
// there is no queue, the first text would be gone before it was read.
// See test_no_tag_finish.py.
bool noTagYellowLineAllowed(int new_spool_id, const char* lost, unsigned parts) {
  if (!s_chip_hint) return true;
  if (parts) logSDf("NoTag: spool %d: %s (parts 0x%02x) not on the status line, the chip hint stays there",
                    new_spool_id, lost, parts);
  else       logSDf("NoTag: spool %d: %s, not on the status line, the chip hint stays there",
                    new_spool_id, lost);
  return false;
}

// ------------------------------------------------------------
//  The question for a label
// ------------------------------------------------------------

#define NTA_BOX_H   160
#define NTA_GAP       8
#define NTA_BTN_W   ((UI_POPUP_W - 2 * UI_CARD_ROW_X - NTA_GAP) / 2)
#define NTA_TITLE_Y  30
#define NTA_BTN_Y   (NTA_BOX_H - UI_POPUP_BTN_H - 18)

bool noTagLabelAskOpen() {
  return s_card != nullptr;
}

void closeNoTagLabelAsk() {
  releaseScreen(&s_card);
  s_card_for = 0;
  s_answer   = LA_NONE;
  s_ask_for  = 0;
  s_ask_wait = false;
  // A picker armed and not yet asked for belongs to the same main screen. One
  // already asked for is the picker's own (requestLocationPickerThen()).
  s_loc_for  = 0;
  s_loc_wait = false;
}

// One answer: "Print" in the house green, "No" quiet, as the other questions.
static lv_obj_t* askButton(lv_obj_t* box, int x, bool print, int text_id) {
  lv_obj_t* btn = lv_btn_create(box);
  lv_obj_set_size(btn, NTA_BTN_W, UI_POPUP_BTN_H);
  lv_obj_set_pos(btn, x, NTA_BTN_Y);
  lv_obj_set_style_bg_color(btn, lv_color_hex(print ? UI_COL_OK_BG : UI_COL_LINE), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(print ? UI_COL_OK_BG_PRESSED : UI_COL_POPUP_BORDER),
                            LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn, UI_RADIUS_BTN, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_t* lbl = lv_label_create(btn);
  lv_label_set_text(lbl, T(text_id));
  lv_obj_set_style_text_color(lbl, lv_color_hex(print ? UI_COL_OK_TEXT : UI_COL_INK_2), 0);
  lv_obj_set_style_text_font(lbl, UI_FONT_TITLE, 0);
  lv_obj_center(lbl);
  return btn;
}

// "Print" is the "More info" -> "Print label" of the spool on the screen, and
// that reads a setting from NVS the first time: read here, so that the print
// the loop starts finds it cached (more_info_screen.cpp does the same).
static void buildAsk(int spool_id) {
  closeNoTagLabelAsk();
  s_card_for = spool_id;
  labelPrinterAskFirst();

  s_card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(s_card, LV_HOR_RES, LV_VER_RES);
  lv_obj_set_pos(s_card, 0, 0);
  lv_obj_set_style_bg_color(s_card, lv_color_hex(UI_COL_SCRIM), 0);
  lv_obj_set_style_bg_opa(s_card, UI_OPA_SCRIM, 0);
  lv_obj_set_style_border_width(s_card, 0, 0);
  lv_obj_set_style_radius(s_card, 0, 0);
  lv_obj_set_style_pad_all(s_card, 0, 0);
  lv_obj_clear_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* box = lv_obj_create(s_card);
  lv_obj_set_size(box, UI_POPUP_W, NTA_BOX_H);
  lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(box, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_border_color(box, lv_color_hex(UI_COL_POPUP_BORDER), 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_radius(box, UI_RADIUS_BOX, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(box);
  lv_label_set_text(title, T(STR_LABEL_ASK));
  lv_obj_set_style_text_color(title, lv_color_hex(UI_COL_INK), 0);
  lv_obj_set_style_text_font(title, UI_FONT_HEADLINE, 0);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(title, UI_POPUP_W - UI_CARD_TEXT_PAD);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, NTA_TITLE_Y);

  // The taps only park the answer: the print starts the Bluetooth stack or
  // the network and blocks for seconds, and the card cannot delete itself
  // from its own button. The tick carries both out.
  lv_obj_add_event_cb(askButton(box, UI_CARD_ROW_X, true, STR_PS_PRINT), [](lv_event_t*) {
    s_answer = LA_PRINT;
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(askButton(box, UI_POPUP_W - NTA_BTN_W - UI_CARD_ROW_X, false, STR_AMS_TIMER_NO), [](lv_event_t*) {
    s_answer = LA_NO;
  }, LV_EVENT_CLICKED, NULL);
  logSDf("NoTag: label question up for spool %d", spool_id);
}

// The picker has ended by itself, after its own way back to the main screen:
// now the question for a label, under its own rules below. The label reads
// the location off the screen, so it carries the one just chosen.
static void locationPickerEnded(int spool_id) {
  s_ask_for  = (shownStill(spool_id) && labelPrinterReadiness() == LP_OK) ? spool_id : 0;
  s_ask_wait = s_ask_for != 0;
  logSDf("NoTag: location picker for spool %d ended, label question %s",
         spool_id, s_ask_for ? "armed" : "not asked");
}

// Wurzel (V39): a spool made without a tag was never asked where it is
// stored. The picker opens unasked only when a tag is lifted ("location on
// removal", and only with a load cell), and this spool has no tag; the label
// asked for right after the create went out without the shelf, or with the
// template's for a copy.
// Fix: the ending arms the picker for the new spool, whatever that setting
// and the load cell say; a pass after it this opens the list of "More info"
// -> "Location" (the choice and the lists were released in the ending's
// pass), for the spool that is still the one on the screen, marked as asked
// so that the removal does not ask it again. The question for a label is
// armed only from locationPickerEnded(); closed without a choice, the
// location stays as it is (a copy's is the template's).
// NOT-TO-DO: arming the question at the ending and holding it while the
// picker stands: the picker's way out is showMainScreen(), whose
// hideAllOverlays() calls closeNoTagLabelAsk(), so the question would be gone.
// Keeping it through that would let it come over a screen a navigation opened.
// See test_no_tag_location.py.
static void askLocation() {
  if (s_loc_wait) { s_loc_wait = false; return; }
  const int id = s_loc_for;
  s_loc_for = 0;
  if (!shownStill(id)) {
    logSDf("NoTag: location picker for spool %d not opened, the screen changed", id);
    return;
  }
  g_loc_popup_shown_for_id = id;
  logSDf("NoTag: location picker for spool %d", id);
  requestLocationPickerThen(id, locationPickerEnded);
}

// Wurzel (U2, decision 9): the loop held the questions after a lift and the
// lift clock of the spool (idSpoolTick()) only while the label card was up.
// Since the picker comes first (V39), a lift during the picker raised the
// bay question over it at once, and the clock ran on behind the picker: in
// the uncovered pass before the card is built the spool ended, and the label
// question and "Print" with it.
// Fix: one predicate for the whole way, each state handing over to the next
// in the same pass: the picker armed (s_loc_for, its pass of delay included),
// asked for or up with the label question waiting on its end (the waiter
// read in more_info_screen.cpp), the question armed (s_ask_for, its pass of
// delay included), the card up. False again once the last answer is taken,
// or a navigation dropped the rest (closeNoTagLabelAsk(), hideMoreInfoOverlays()).
// NOT-TO-DO: a flag of our own set when the picker is asked for and cleared
// in locationPickerEnded(): a navigation or another request drops the waiter
// without calling it, and the flag would hold the questions and the clock
// for good.
// See test_no_tag_finish.py.
bool noTagAfterCreateBusy() {
  return s_loc_for != 0 || locationPickerThenPending(locationPickerEnded) ||
         s_ask_for != 0 || noTagLabelAskOpen();
}

// Wurzel: the question is about the spool that was just made, and a label is
// printed "for the screen". A tag laid on while the card stands, or any other
// spool that takes the screen, would turn "Print" into the label of something
// else.
// Fix: the card lives only while the new spool is the one shown by its number,
// and the answer is checked against the screen again when it is carried out.
// The card is built a pass after it was armed, not in that pass: what the
// picker's way back released there goes back to the pool only after LVGL has
// run (print_sheet.cpp and confirm_popup.cpp wait a pass the same way). While
// the picker is armed and not yet asked for, the question waits behind it.
// NOT-TO-DO: printing in the button's callback, or on the answer without
// asking the screen again: the tap can be a pass old.
// See test_no_tag_finish.py and test_no_tag_location.py.
void noTagLabelAskTick() {
  if (s_card) {
    const LabelAnswer answer = s_answer;
    if (answer == LA_NONE && shownStill(s_card_for)) return;
    const bool print = answer == LA_PRINT && shownStill(s_card_for);
    logSDf("NoTag: label question for spool %d closed, %s", s_card_for,
           print ? "print" : (answer == LA_NO ? "no" : "the screen changed"));
    closeNoTagLabelAsk();
    if (print) {
      if (labelPrinterAskFirst()) show_print_sheet_pending = true;
      else                        print_spool_label_pending = true;
    }
    return;
  }
  if (s_loc_for) { askLocation(); return; }
  if (!s_ask_for) return;
  if (s_ask_wait) { s_ask_wait = false; return; }
  const int id = s_ask_for;
  s_ask_for = 0;
  if (shownStill(id) && labelPrinterReadiness() == LP_OK) buildAsk(id);
}
