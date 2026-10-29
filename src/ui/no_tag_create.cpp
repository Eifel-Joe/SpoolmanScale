#include "ui/no_tag_create.h"

#include <lvgl.h>
#include <stdio.h>

#include "app/app_loop.h"
#include "app/app_state.h"
#include "app/deferred_actions.h"
#include "app/id_spool.h"
#include "hardware/sd_logger.h"
#include "services/batch_create_rules.h"
#include "services/label_printer.h"
#include "services/location_state.h"
#include "services/no_tag_create_rules.h"
#include "ui/batch_create.h"
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

// Wurzel: a count typed on a card (batch_create.h) belongs to that one create,
// and a cancel leaves the flag on (no_tag_create.h). The next "Create spool"
// sets it on again, which is no change: reset on a change only, the old count
// would make that many spools in the next create.
// Fix: back to one whenever the flag is set. Every caller is a way into a
// create or its end, none is on a card.
// NOT-TO-DO: the reset after the early return below, on a change only.
// See test_batch_flow.py (the card).
void noTagModeSet(bool on) {
  batchCountReset();
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
// follow the ending. After a run of several spools the overview comes first,
// and the location chosen goes to the others before the question.
enum LabelAnswer : uint8_t { LA_NONE = 0, LA_PRINT, LA_NO, LA_NEXT };

#define NTO_REASON_MAX 96                   // as BATCH_REASON_MAX in batch_create.cpp

static lv_obj_t*   s_card     = nullptr;    // the scrim with the box on it
static int         s_card_for = 0;          // the spool the card asks about
static bool        s_card_over = false;     // the card is a run's overview, not the label question
static LabelAnswer s_answer   = LA_NONE;    // parked by a button, taken by the tick
static int         s_ask_for  = 0;          // armed when the picker ended: ask for this spool, 0: none
static bool        s_ask_wait = false;      // the card is built a pass after it was armed
static int         s_loc_for  = 0;          // armed by the ending: the picker for this spool, 0: none
static bool        s_loc_wait = false;      // the picker is asked for a pass after it was armed
static bool        s_chip_hint = false;     // the status line holds the Bambu chip hint
static int         s_over_for  = 0;         // armed by a run's ending: the overview for this spool, 0: none
static bool        s_over_wait = false;     // the overview is built a pass after it was armed
static int         s_over_wanted = 0;       // the count the run was asked for
static char        s_over_reason[NTO_REASON_MAX] = "";   // why it stopped, "" when all were made
static int         s_rest_for  = 0;         // armed when the picker wrote: its location to the rest of the run
static bool        s_rest_wait = false;     // ... a pass after the picker's way back
static bool        s_rest_clear = false;    // ... it was "no location"
static char        s_rest_loc[LOCATION_NAME_MAX] = "";

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

// Wurzel: a run of several spools ended in noTagCreateFinish() for the first,
// whose picker and question for a label are about one spool: the others got
// neither a shelf nor a label, and which numbers were made, or why the run
// stopped, was only in the log.
// Fix: the first spool is shown as after one create and the run is kept
// (batchRunKeep()); the picker the ending armed becomes the overview, which
// names every number and the reason of a stop, and whose "Next" arms the
// picker for the first spool as the ending would have. The location chosen
// there goes to the others once the picker has ended (locationPickerEnded()),
// and the question for a label then asks for all of them. The overview is
// armed exactly when the picker would have been: with the first spool on the
// screen; otherwise the log has it all.
// NOT-TO-DO: the overview after the picker: a shelf would be chosen before
// the user learns which spools it is for, and after a stop how few they are.
// See test_batch_flow.py (after the run).
void noTagBatchFinish(const int* ids, int made, int wanted, const char* vendor, const char* reason) {
  if (!ids || made < 1) return;
  noTagCreateFinish(ids[0], vendor);
  batchRunKeep(ids, made);
  s_over_wanted = wanted;
  snprintf(s_over_reason, sizeof(s_over_reason), "%s", reason ? reason : "");
  s_over_for  = s_loc_for;
  s_over_wait = s_loc_wait;
  s_loc_for   = 0;
  s_loc_wait  = false;
  char list[BATCH_IDS_TEXT_MAX];
  if (!batchIdsText(ids, made, list, sizeof(list))) list[0] = '\0';
  logSDf("NoTag: run of %d made %d: %s%s%s, overview %s", wanted, made, list,
         (reason && reason[0]) ? ", stopped: " : "", reason ? reason : "",
         s_over_for ? "armed" : "not shown");
  if (made < 2) return;
  char msg[96];
  snprintf(msg, sizeof(msg), T(s_chip_hint ? STR_BATCH_CREATED_BAMBU : STR_BATCH_CREATED), made);
  statusMessageShow(msg, UI_COL_GOOD);
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
  s_card_over = false;
  s_answer   = LA_NONE;
  s_ask_for  = 0;
  s_ask_wait = false;
  // A picker armed and not yet asked for belongs to the same main screen. One
  // already asked for is the picker's own (requestLocationPickerThen()).
  s_loc_for  = 0;
  s_loc_wait = false;
  // So does an overview not yet built. The location for the rest of a run is
  // no question: it was chosen, and is written whatever the screen shows.
  s_over_for  = 0;
  s_over_wait = false;
}

// The scrim over the screen with the box on it, h high, in the house look;
// the scrim is s_card. Hands back the box.
static lv_obj_t* cardBox(lv_obj_t* screen, lv_coord_t h) {
  s_card = lv_obj_create(screen);
  lv_obj_set_size(s_card, LV_HOR_RES, LV_VER_RES);
  lv_obj_set_pos(s_card, 0, 0);
  lv_obj_set_style_bg_color(s_card, lv_color_hex(UI_COL_SCRIM), 0);
  lv_obj_set_style_bg_opa(s_card, UI_OPA_SCRIM, 0);
  lv_obj_set_style_border_width(s_card, 0, 0);
  lv_obj_set_style_radius(s_card, 0, 0);
  lv_obj_set_style_pad_all(s_card, 0, 0);
  lv_obj_clear_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* box = lv_obj_create(s_card);
  lv_obj_set_size(box, UI_POPUP_W, h);
  lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(box, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_border_color(box, lv_color_hex(UI_COL_POPUP_BORDER), 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_radius(box, UI_RADIUS_BOX, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  return box;
}

// A centred text of the card's text width at y from the box's top, wrapping.
static lv_obj_t* cardText(lv_obj_t* box, const char* text, const lv_font_t* font, uint32_t color,
                          lv_coord_t y) {
  lv_obj_t* l = lv_label_create(box);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, UI_POPUP_W - UI_CARD_TEXT_PAD);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
  return l;
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
// the loop starts finds it cached (more_info_screen.cpp does the same). For
// the first spool of a run the question names how many labels it prints.
static void buildAsk(int spool_id) {
  closeNoTagLabelAsk();
  s_card_for = spool_id;
  labelPrinterAskFirst();

  lv_obj_t* box = cardBox(lv_scr_act(), NTA_BOX_H);
  const int run = batchRunOf(spool_id, nullptr);
  char title[48];
  if (run > 1) snprintf(title, sizeof(title), T(STR_BATCH_LABEL_ASK), run);
  else         snprintf(title, sizeof(title), "%s", T(STR_LABEL_ASK));
  cardText(box, title, UI_FONT_HEADLINE, UI_COL_INK, NTA_TITLE_Y);

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

// The overview of a run, in the house card's rows: the icon (a stop in
// yellow), "5 made" or "3 of 5 made", every number as one text, under it the
// reason of a stop in red, and "Next" across. The tap only parks it.
#define NTO_REASON_GAP 6    // between the numbers and the reason under them

static void buildOverview(int spool_id) {
  const int* ids = nullptr;
  const int made = batchRunOf(spool_id, &ids);
  closeNoTagLabelAsk();
  s_card_for  = spool_id;
  s_card_over = true;
  const bool stopped = made < s_over_wanted;

  lv_obj_t* box = cardBox(lv_scr_act(), UI_CARD_H);
  lv_obj_t* icon = lv_label_create(box);
  lv_label_set_text(icon, stopped ? LV_SYMBOL_WARNING : LV_SYMBOL_OK);
  lv_obj_set_style_text_color(icon, lv_color_hex(stopped ? UI_COL_WARN : UI_COL_GOOD), 0);
  lv_obj_set_style_text_font(icon, UI_FONT_ICON, 0);
  lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, UI_CARD_ICON_Y);

  char title[64];
  if (stopped) snprintf(title, sizeof(title), T(STR_BATCH_OVERVIEW_SOME), made, s_over_wanted);
  else         snprintf(title, sizeof(title), T(STR_BATCH_OVERVIEW_ALL), made);
  cardText(box, title, UI_FONT_HEADLINE, UI_COL_INK, UI_CARD_TITLE_Y);
  char list[BATCH_IDS_TEXT_MAX];
  if (!batchIdsText(ids, made, list, sizeof(list))) list[0] = '\0';
  lv_obj_t* nums = cardText(box, list, UI_FONT_BODY, UI_COL_INK_2, UI_CARD_TEXT_Y);
  if (s_over_reason[0]) {
    lv_obj_t* why = cardText(box, s_over_reason, UI_FONT_BODY, UI_COL_BAD_TEXT, 0);
    lv_obj_align_to(why, nums, LV_ALIGN_OUT_BOTTOM_MID, 0, NTO_REASON_GAP);
  }

  lv_obj_t* next = askButton(box, UI_CARD_ROW_X, true, STR_BTN_NEXT);
  lv_obj_set_size(next, UI_POPUP_W - 2 * UI_CARD_ROW_X, UI_POPUP_BTN_H);
  lv_obj_set_y(next, UI_CARD_ROW_Y);
  lv_obj_add_event_cb(next, [](lv_event_t*) {
    s_answer = LA_NEXT;
  }, LV_EVENT_CLICKED, NULL);
  logSDf("NoTag: overview up for spool %d, %d of %d made: %s", spool_id, made, s_over_wanted, list);
}

// A pass after the run's ending, as the picker would have come: the overview
// for the first spool, still the one on the screen.
static void askOverview() {
  if (s_over_wait) { s_over_wait = false; return; }
  const int id = s_over_for;
  s_over_for = 0;
  if (!shownStill(id)) {
    logSDf("NoTag: overview for spool %d not shown, the screen changed", id);
    return;
  }
  buildOverview(id);
}

// The picker has ended by itself, after its own way back to the main screen:
// now the question for a label, under its own rules below. The label reads
// the location off the screen, so it carries the one just chosen.
// After a run, what the picker wrote to the first spool goes to the others
// first: the picker keeps it (locationPickerChosen(), only a write that was
// answered with success), and only for the first spool still on the screen;
// a tag laid on meanwhile took the picker's write. Closed without a choice,
// every spool keeps its own location.
static void locationPickerEnded(int spool_id) {
  bool cleared = false;
  if (shownStill(spool_id) && batchRunOf(spool_id, nullptr) > 1 &&
      locationPickerChosen(s_rest_loc, sizeof(s_rest_loc), &cleared)) {
    s_rest_for = spool_id; s_rest_clear = cleared; s_rest_wait = true;
  }
  s_ask_for  = (shownStill(spool_id) && labelPrinterReadiness() == LP_OK) ? spool_id : 0;
  s_ask_wait = s_ask_for != 0;
  logSDf("NoTag: location picker for spool %d ended, %slabel question %s", spool_id,
         s_rest_for ? "location to the rest of the run, " : "", s_ask_for ? "armed" : "not asked");
}

// The location to the rest of the run, a pass after the picker's way back
// (what it released goes back to the pool only after LVGL has run, and the
// loading overlay is built anew). The question for a label waits behind it.
static void writeRest() {
  if (s_rest_wait) { s_rest_wait = false; return; }
  const int id = s_rest_for;
  s_rest_for = 0;
  batchLocationToRest(id, s_rest_clear ? nullptr : s_rest_loc);
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
// After a run three more: the overview armed (s_over_for; up, it is the
// card), the location for the rest armed (s_rest_for), and its labels being
// printed (batchLabelsBusy()); each of them ends by itself.
// NOT-TO-DO: a flag of our own set when the picker is asked for and cleared
// in locationPickerEnded(): a navigation or another request drops the waiter
// without calling it, and the flag would hold the questions and the clock
// for good.
// See test_no_tag_finish.py and test_batch_flow.py (after the run).
bool noTagAfterCreateBusy() {
  return s_loc_for != 0 || locationPickerThenPending(locationPickerEnded) ||
         s_ask_for != 0 || noTagLabelAskOpen() ||
         s_over_for != 0 || s_rest_for != 0 || batchLabelsBusy();
}

// "Print" for the first spool of a run: with the preview on, the sheet for
// the first, whose own print goes on with the others; else every label in a
// row, from the first.
static void printRun(int first_id) {
  if (labelPrinterAskFirst()) { batchLabelsAfterSheet(first_id); show_print_sheet_pending = true; }
  else                        batchLabelsStart(LABEL_KIND_SPOOL, 0);
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
// A run's overview is the same card under the same rules; its "Next" arms the
// picker for its spool. Behind the picker come the overview and the location
// for the rest, each in the order of the way.
// NOT-TO-DO: printing in the button's callback, or on the answer without
// asking the screen again: the tap can be a pass old.
// See test_no_tag_finish.py, test_no_tag_location.py and test_batch_flow.py.
void noTagLabelAskTick() {
  if (s_card) {
    const LabelAnswer answer = s_answer;
    if (answer == LA_NONE && shownStill(s_card_for)) return;
    const bool print = answer == LA_PRINT && shownStill(s_card_for);
    const bool next  = answer == LA_NEXT && shownStill(s_card_for);
    const int  asked = s_card_for;
    logSDf("NoTag: %s for spool %d closed, %s", s_card_over ? "overview" : "label question", asked,
           next ? "next" : print ? "print" : (answer == LA_NO ? "no" : "the screen changed"));
    closeNoTagLabelAsk();
    if (next) { s_loc_for = asked; s_loc_wait = true; return; }
    if (print && batchRunOf(asked, nullptr) > 1) { printRun(asked); return; }
    if (print) {
      if (labelPrinterAskFirst()) show_print_sheet_pending = true;
      else                        print_spool_label_pending = true;
    }
    return;
  }
  if (s_loc_for) { askLocation(); return; }
  if (s_over_for) { askOverview(); return; }
  if (s_rest_for) { writeRest(); return; }
  if (!s_ask_for) return;
  if (s_ask_wait) { s_ask_wait = false; return; }
  const int id = s_ask_for;
  s_ask_for = 0;
  if (shownStill(id) && labelPrinterReadiness() == LP_OK) buildAsk(id);
}
