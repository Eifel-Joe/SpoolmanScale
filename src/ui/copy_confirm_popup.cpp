#include "ui/spool_flow_internal.h"
#include "spool_flow.h"
#include "ui_common.h"
#include "app/app_state.h"

#include <Arduino.h>
#include <lvgl.h>
#include <cmath>
#include <cstring>

#include "hardware/sd_logger.h"
#include "services/copy_bbs.h"      // what a copy behind Bambuddy on Spoolman takes over
#include "services/spool_start.h"
#include "lang.h"
#include "services/spool_extras.h"
#include "services/spool_types.h"   // spoolTypesActive(), NEW_SPOOL_TARE_MIN_G
#include "ui/info_popup.h"
#include "ui/price_pad.h"
#include "ui/spool_type_list.h"
#include "ui/theme.h"

// ============================================================
//  THE COPY CONFIRMATION
//
//  The last question of the copy flow, whichever way the template was
//  chosen: the Bambu copy list, the picker, or its id. Out of copy_flow.cpp
//  since v0.8.0-beta.49, when it moved onto the house card.
//
//  With Bambuddy on Spoolman and a spool on the pad, the maker's list of
//  spool types comes first (design notes L21, L22): the type picked is the
//  copy's empty spool, on the card and on the server; "without type" leaves
//  the template's (copy_bbs.h says which of it is sent); back goes where
//  Cancel on the card goes. The card is built when the list answers.
//
//  With Bambuddy on Spoolman the template is read before all of that, for
//  each of the three ways it can be chosen (design note J, Festlegung 15),
//  and copy_bbs.h holds what was read until the create or until the card is
//  left. The card then shows what the copy takes over - the profile, the
//  place and the supplier - and has a price button between its answers
//  (section 3). Every way off the card that creates nothing clears what was
//  read: Cancel, back from the list of types, a read that failed; the create
//  clears it in doCopySpoolCreate(). A card closed from outside the flow
//  (hideSpoolFlowOverlays()) leaves it to the next card: behind Bambuddy on
//  Spoolman its read replaces it, outside it the card clears it as it opens.
//  Nothing is created from it in between, a create comes only from this
//  card and this card only after that.
// ============================================================

// The lines of the card.
#define COPY_CARD_LINE_H   26
#define COPY_CARD_SWATCH   18
#define COPY_CARD_GAP      8
#define COPY_CARD_TEXT_W   (UI_POPUP_W - UI_CARD_TEXT_PAD)
// Behind Bambuddy on Spoolman (design note J, section 3): two lines more in
// the small type of line 2, and the card grows by them. Festlegung 13: five
// lines do not fit the house card's 260 px, so it is 304 of the screen's 320
// with the answers at the same distance from its bottom edge. The answers get
// the price between them, as on the card for a new spool from a tag.
#define COPY_CARD_SMALL_PITCH  22   // UI_FONT_SMALL's 16 px line and 6 px of air
#define COPY_CARD_BBS_GROW     (2 * COPY_CARD_SMALL_PITCH)
#define COPY_CARD_PRICE_W      80
#define COPY_CARD_PRICE_TEXT_W (COPY_CARD_PRICE_W - 8)   // 4 px of air each side (ui_budgets.tsv)
#define COPY_CARD_ANSWER_W     ((UI_POPUP_W - 2 * UI_CARD_ROW_X - 2 * COPY_CARD_GAP - COPY_CARD_PRICE_W) / 2)

lv_obj_t *scr_copy_confirm = nullptr;  // confirm popup

// Template selected for copy
static int   copy_template_filament_id = 0;
static int   copy_template_spool_id    = 0;
static float copy_template_initial     = 0;
static float copy_template_spool_w     = 0;
static char  copy_template_name[64]    = "";
// A copy of the caller's look: the card may be built once the list of types
// has answered, when a look the caller kept on its stack is gone.
static CopyLook copy_template_look     = {};
static bool     copy_template_has_look = false;
// The spool type chosen for the copy (BBS), 0: none, the template's.
static float    s_copy_core            = 0.0f;
// The template is behind Bambuddy on Spoolman and was read for this card
// (copyBbsLoad()): the card shows what the copy takes over.
static bool     s_copy_bbs             = false;
// The price button (BBS), a price per kg (Festlegung 5). copyBbsView() gives
// the template's, never a typed one, so what was typed since the card was
// asked for is kept here: for the button, and for a card built again within
// the same copy, which must show what copy_bbs.h will send.
static lv_obj_t* s_copy_price_lbl      = nullptr;
static float     s_copy_price_tpl      = 0.0f;   // the template's price per kg
static bool      s_copy_price_typed    = false;
static float     s_copy_price          = 0.0f;   // typed per kg; 0: emptied, no price
// The list of types was answered for this copy, a type or "without type":
// copyCreateRequest() takes it along with the values for the create, which
// marks the new spool for the first weighing (tareEntryFirstWeighDone()).
// Set with s_copy_core, cleared with it for every new confirmation.
static bool     s_copy_type_asked      = false;

bool copyTypeAsked() { return s_copy_type_asked; }

void closeCopyConfirmPopup() {
  releaseScreen(&scr_copy_confirm);
  s_copy_price_lbl = nullptr;
}

// The empty spool the copy starts from: the type chosen, else the template's.
static float copyTare() {
  return s_copy_core > 0.0f ? s_copy_core : copy_template_spool_w;
}

// Cancel on the card, and back in the list of types: to whichever list the
// template came from. What was read for the card goes as well; a template
// chosen again is read again.
static void copyBackToList() {
  closeCopyConfirmPopup();
  copyBbsClear();
  if (scr_copy_list) lv_obj_clear_flag(scr_copy_list, LV_OBJ_FLAG_HIDDEN);
  linkPickerShowList();
}

// The price button shows the price per kg that goes to the server: the one
// typed, else the template's ("10.99/kg"), and "Price" where there is none
// (Festlegung 5: 0 is no price).
//
// Wurzel: the button is 80 px and its text gets 72 of them, and with cents a
// price per kg of 100 or more can be wider than that in UI_FONT_SMALL
// ("484.04/kg" is 74 px); most of 1000 or more are (up to 83 px), and a
// currency with small units reaches four digits for a kg of filament. A text
// wider than the button is clipped at both ends. Fix: cents while the text
// fits, else whole units, which hold five digits ("48484/kg" is 71 px), more
// than the pad takes (9999.99). Measured rather than counted in digits, as
// digits differ in width ("1" is 5 px, "4" is 9). NOT-TO-DO: leaving out
// "/kg", the very thing the button says now; a smaller font, which would set
// this one label apart from the answers beside it.
static void copyPriceRefresh() {
  if (!s_copy_price_lbl) return;
  const float price = s_copy_price_typed ? s_copy_price : s_copy_price_tpl;
  if (!(price > 0.0f)) {
    lv_label_set_text(s_copy_price_lbl, T(STR_TAGNEW_PRICE_BTN));
    return;
  }
  char buf[48];   // "%.2f" of the largest float is 42 characters, nothing is cut
  snprintf(buf, sizeof(buf), T(STR_COPY_CARD_PRICE_KG), price);
  lv_point_t size = { 0, 0 };
  lv_txt_get_size(&size, buf, UI_FONT_SMALL, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  if (size.x > COPY_CARD_PRICE_TEXT_W) snprintf(buf, sizeof(buf), T(STR_COPY_CARD_PRICE_KG_WHOLE), price);
  lv_label_set_text(s_copy_price_lbl, buf);
}

// The pad's answer, from the loop (pricePadTick()): the price per kg of the
// new spool, 0 for an emptied field; a cancelled pad does not call. A card
// that was closed meanwhile takes no price: nothing is created from it.
//
// Wurzel: OK calls back with whatever the field holds, also with the value it
// was filled in with, and that is the template's price per kg cut to the cent
// ("%.2f"), not a price anyone typed. Fix: nothing typed before and the value
// within half a cent of the template's price per kg is no typed price; the
// template's value goes on untouched (copy_rules.h) unless a different price
// was typed. NOT-TO-DO: copyBbsSetPrice() for it, which would hand the shown
// cent on as typed in place of the template's own value.
static void copyPriceTyped(float price) {
  if (!scr_copy_confirm || !s_copy_bbs) return;
  if (!s_copy_price_typed && fabsf(price - s_copy_price_tpl) < 0.005f) return;
  s_copy_price_typed = true;
  s_copy_price       = price;
  copyBbsSetPrice(price);
  logSDf("Copy: price %.2f per kg typed for the copy of spool %d", price, copy_template_spool_id);
  copyPriceRefresh();
}

static void buildCopyPriceButton(lv_obj_t* box, int y) {
  lv_obj_t* btn = lv_btn_create(box);
  lv_obj_set_size(btn, COPY_CARD_PRICE_W, UI_POPUP_BTN_H);
  lv_obj_set_pos(btn, UI_CARD_ROW_X + COPY_CARD_ANSWER_W + COPY_CARD_GAP, y);
  lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COL_ROW), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COL_ROW_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(UI_COL_LINE), 0);
  lv_obj_set_style_radius(btn, UI_RADIUS_BTN, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_add_event_cb(btn, [](lv_event_t*) {
    logSD("BTN: CopyConfirm -> Price");
    // Filled in with the price per kg the button stands for, to the cent,
    // under a title that says it is one.
    showPricePadTitled(s_copy_price_typed ? s_copy_price : s_copy_price_tpl,
                       STR_PRICE_PAD_TITLE_KG, copyPriceTyped);
  }, LV_EVENT_CLICKED, NULL);
  s_copy_price_lbl = lv_label_create(btn);
  lv_obj_set_style_text_color(s_copy_price_lbl, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_style_text_font(s_copy_price_lbl, UI_FONT_SMALL, 0);
  lv_obj_center(s_copy_price_lbl);
  copyPriceRefresh();
}

// One answer of the card: create in green on the left, as every confirming
// answer is, cancel in red on the right.
static lv_obj_t* copyAnswerButton(lv_obj_t* box, int x, int y, int w, bool ok) {
  lv_obj_t *btn = lv_btn_create(box);
  lv_obj_set_size(btn, w, UI_POPUP_BTN_H);
  lv_obj_set_pos(btn, x, y);
  lv_obj_set_style_bg_color(btn, lv_color_hex(ok ? UI_COL_OK_BG : UI_COL_BAD_BG), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(ok ? UI_COL_OK_BG_PRESSED : UI_COL_BAD_BG_PRESSED),
                            LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn, UI_RADIUS_BTN, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, T(ok ? STR_COPY_CARD_CREATE : STR_CANCEL));
  lv_obj_set_style_text_color(lbl, lv_color_hex(ok ? UI_COL_OK_TEXT : UI_COL_BAD_TEXT), 0);
  lv_obj_set_style_text_font(lbl, UI_FONT_TITLE, 0);
  lv_obj_center(lbl);
  return btn;
}

// A line in line 2's type: small, soft, centred. With one_line it is exactly
// one line high and dotted when it runs long.
//
// Wurzel: LVGL dots a LONG_DOT label only where its text is taller than the
// label (lv_label_refr_text); with the height left to the content the label
// grows instead, and a long text wraps - a long place and supplier ran over
// line 3. Fix: one line of height, as labelOneLine() gives its labels.
// NOT-TO-DO: the same for line 2 of the card outside Bambuddy on Spoolman,
// which stays exactly as it was (ruling 16).
static lv_obj_t* copyCardSmallLine(lv_obj_t* box, const char* text, int y, bool one_line) {
  lv_obj_t *lbl = lv_label_create(box);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_color(lbl, lv_color_hex(UI_COL_INK_SOFT), 0);
  lv_obj_set_style_text_font(lbl, UI_FONT_SMALL, 0);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(lbl, COPY_CARD_TEXT_W);
  if (one_line) lv_obj_set_height(lbl, lv_font_get_line_height(UI_FONT_SMALL));
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, y);
  return lbl;
}

// The two lines a copy behind Bambuddy on Spoolman adds (section 3), from y
// down: the profile with the profiles per printer model counted behind it,
// and the place with the supplier. A dash for what the template has not; the
// line stays, so the card says that nothing is taken over there.
//
// Wurzel: one label dots its end, and the end of the profile line is the
// count - the one thing on it that says profiles per model go along ("Profil:
// Tinmorry PETG schwarz @ A1 (Custom)" with one of them is 404 px of a
// 360 px line). Fix: the count is a label of its own that keeps its width,
// and the name beside it gets the rest, as the swatch and the name of line 1
// share theirs. NOT-TO-DO: putting the count first, which turns the line
// round from the order the design note shows.
static void buildCopyTakenOver(lv_obj_t* box, const CopyBbsView& v, int y) {
  char count[32] = "";
  if (v.model_presets > 0) {
    snprintf(count, sizeof(count),
             T(v.model_presets == 1 ? STR_COPY_CARD_MODEL : STR_COPY_CARD_MODELS), v.model_presets);
  }
  char name[96], profile[128];
  utf8Cut(v.profile_name[0] ? v.profile_name : "-", sizeof(name) - 1, name, sizeof(name));
  snprintf(profile, sizeof(profile), T(STR_COPY_CARD_PROFILE), name);

  lv_obj_t *row = lv_obj_create(box);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, COPY_CARD_TEXT_W, lv_font_get_line_height(UI_FONT_SMALL));
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_point_t count_size = { 0, 0 };
  if (count[0]) {
    lv_txt_get_size(&count_size, count, UI_FONT_SMALL, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  }
  lv_obj_t *lbl = labelOneLine(row, profile, UI_FONT_SMALL, COPY_CARD_TEXT_W - count_size.x);
  lv_obj_set_style_text_color(lbl, lv_color_hex(UI_COL_INK_SOFT), 0);
  if (count[0]) {
    lbl = labelOneLine(row, count, UI_FONT_SMALL, COPY_CARD_TEXT_W);
    lv_obj_set_style_text_color(lbl, lv_color_hex(UI_COL_INK_SOFT), 0);
  }

  // Where it is kept, and where it is bought: whole letters into the
  // buffers, and one line on the card that dots what is wider than it.
  char loc[64], sup[64], place[136];
  utf8Cut(v.location[0] ? v.location : "-", sizeof(loc) - 1, loc, sizeof(loc));
  utf8Cut(v.supplier[0] ? v.supplier : "-", sizeof(sup) - 1, sup, sizeof(sup));
  snprintf(place, sizeof(place), "%s | %s", loc, sup);
  copyCardSmallLine(box, place, y + COPY_CARD_SMALL_PITCH, true);
}

// The copy confirmation, on the house card for a question (400 x 260, the
// answers in its bottom row): the template as a colour and a name, where it
// comes from, and what the new spool will weigh. It used to be one block of
// text on a box of its own, and the picker asked a second question first.
// Behind Bambuddy on Spoolman 400 x 304: the profile and the place with the
// supplier under line 2, and create | price | cancel as its answers.
static void buildCopyCard() {
  closeCopyConfirmPopup();
  const CopyLook* look = copy_template_has_look ? &copy_template_look : nullptr;
  const float tare = copyTare();
  // Everything under line 2 moves down by what the two lines take.
  const int grow = s_copy_bbs ? COPY_CARD_BBS_GROW : 0;
  const int y2   = UI_CARD_TEXT_Y - 4 + COPY_CARD_LINE_H + 4;

  scr_copy_confirm = lv_obj_create(lv_scr_act());
  lv_obj_set_size(scr_copy_confirm, LV_HOR_RES, LV_VER_RES);
  lv_obj_set_pos(scr_copy_confirm, 0, 0);
  lv_obj_set_style_bg_color(scr_copy_confirm, lv_color_hex(UI_COL_SCRIM), 0);
  lv_obj_set_style_bg_opa(scr_copy_confirm, UI_OPA_SCRIM, 0);
  lv_obj_set_style_border_width(scr_copy_confirm, 0, 0);
  lv_obj_set_style_radius(scr_copy_confirm, 0, 0);
  lv_obj_set_style_pad_all(scr_copy_confirm, 0, 0);
  lv_obj_clear_flag(scr_copy_confirm, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *box = lv_obj_create(scr_copy_confirm);
  lv_obj_set_size(box, UI_POPUP_W, UI_CARD_H + grow);
  lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(box, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_border_color(box, lv_color_hex(UI_COL_POPUP_BORDER), 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_radius(box, UI_RADIUS_BOX, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *icon = lv_label_create(box);
  lv_label_set_text(icon, LV_SYMBOL_COPY);
  lv_obj_set_style_text_color(icon, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_text_font(icon, UI_FONT_ICON, 0);
  lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, UI_CARD_ICON_Y);

  lv_obj_t *title = lv_label_create(box);
  lv_label_set_text(title, T(STR_COPY_CONFIRM_TITLE));
  lv_obj_set_style_text_color(title, lv_color_hex(UI_COL_INK), 0);
  lv_obj_set_style_text_font(title, UI_FONT_HEADLINE, 0);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(title, UI_POPUP_W - UI_CARD_TEXT_PAD);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, UI_CARD_TITLE_Y);

  // Line 1: the colour and the filament, as one centred group.
  lv_obj_t *row = lv_obj_create(box);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, UI_POPUP_W - UI_CARD_TEXT_PAD, COPY_CARD_LINE_H);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, UI_CARD_TEXT_Y - 4);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, COPY_CARD_GAP, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

  if (look && look->color_hex[0]) {
    lv_obj_t *sw = lv_obj_create(row);
    lv_obj_set_size(sw, COPY_CARD_SWATCH, COPY_CARD_SWATCH);
    lv_obj_set_style_radius(sw, 4, 0);
    lv_obj_set_style_border_width(sw, 1, 0);
    lv_obj_set_style_border_color(sw, lv_color_hex(UI_COL_POPUP_BORDER), 0);
    lv_obj_set_style_pad_all(sw, 0, 0);
    lv_obj_clear_flag(sw, LV_OBJ_FLAG_SCROLLABLE);
    swatchPaintHex(sw, look->color_hex);
  }

  char line1[80];
  if (look) {
    joinMaterialName(look->material, look->name, line1, sizeof(line1));
  } else {
    snprintf(line1, sizeof(line1), "%s", copy_template_name);
  }
  lv_obj_t *lbl1 = lv_label_create(row);
  lv_label_set_text(lbl1, line1);
  lv_obj_set_style_text_color(lbl1, lv_color_hex(UI_COL_INK), 0);
  lv_obj_set_style_text_font(lbl1, UI_FONT_TITLE, 0);
  lv_label_set_long_mode(lbl1, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_width(lbl1, UI_POPUP_W - UI_CARD_TEXT_PAD - COPY_CARD_SWATCH - COPY_CARD_GAP, 0);

  // Line 2: whose it is and which spool it is copied from.
  char line2[64];
  snprintf(line2, sizeof(line2), T(STR_COPY_CARD_TEMPLATE),
           (look && look->vendor[0]) ? look->vendor : "-", copy_template_spool_id);
  // One line where the profile follows it (BBS); as it always was elsewhere.
  copyCardSmallLine(box, line2, y2, s_copy_bbs);

  // Line 3: what the new spool starts with, as doCopySpoolCreate() will file
  // it (spool_start.h), and the empty spool it is weighed against: the type
  // chosen, else the template's (copyTare()). The reading is the one without
  // the parts on the spool (spool_extras.h), as doCopySpoolCreate() takes it.
  const SpoolStart start = spoolStartValues(spoolExtrasBareReading(), (int)lroundf(tare),
                                            tare > 0.0f ? TARE_Q_KNOWN : TARE_Q_NONE,
                                            (int)lroundf(copy_template_initial));

  // Behind Bambuddy on Spoolman, between lines 2 and 3: what the copy takes
  // over, and its price per kg for the button, the same whatever the label.
  if (s_copy_bbs) {
    CopyBbsView view;
    copyBbsView(&view);   // dashes and no price when nothing is loaded
    s_copy_price_tpl = view.cost_per_kg;
    buildCopyTakenOver(box, view, y2 + COPY_CARD_SMALL_PITCH);
  }
  char line3[80];
  if (start.verdict == START_PARTIAL) {
    snprintf(line3, sizeof(line3), T(STR_COPY_CARD_WEIGHT), start.remaining_g, tare);
  } else {
    char weight[64];
    spoolStartText(start, false, weight, sizeof(weight));
    if (start.verdict == START_FULL) snprintf(line3, sizeof(line3), T(STR_COPY_CARD_TARE), weight, tare);
    else                             snprintf(line3, sizeof(line3), "%s", weight);
  }
  lv_obj_t *lbl3 = lv_label_create(box);
  lv_label_set_text(lbl3, line3);
  lv_obj_set_style_text_color(lbl3, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_style_text_font(lbl3, UI_FONT_BODY, 0);
  lv_obj_set_style_text_align(lbl3, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(lbl3, UI_POPUP_W - UI_CARD_TEXT_PAD);
  lv_obj_align(lbl3, LV_ALIGN_TOP_MID, 0, UI_CARD_TEXT_Y - 4 + 2 * COPY_CARD_LINE_H + 8 + grow);

  // The answers, in the card's bottom row: two of UI_POPUP_BTN_W, or behind
  // Bambuddy on Spoolman two narrower ones with the price between them.
  const int row_y = UI_CARD_ROW_Y + grow;
  const int ans_w = s_copy_bbs ? COPY_CARD_ANSWER_W : UI_POPUP_BTN_W;
  lv_obj_t *btn_ok = copyAnswerButton(box, UI_CARD_ROW_X, row_y, ans_w, true);
  lv_obj_add_event_cb(btn_ok, [](lv_event_t *e) {
    // The handler closes the three popups - this button's own among them -
    // and then creates the spool from the loop.
    copyCreateRequest(copy_template_spool_id, copy_template_filament_id,
                      copy_template_initial, copyTare(), s_copy_core);
  }, LV_EVENT_CLICKED, NULL);

  if (s_copy_bbs) buildCopyPriceButton(box, row_y);

  lv_obj_t *btn_no = copyAnswerButton(box, UI_POPUP_W - ans_w - UI_CARD_ROW_X, row_y, ans_w, false);
  lv_obj_add_event_cb(btn_no, [](lv_event_t *e) {
    logSD("BTN: CopyConfirm -> Cancel (back to list)");
    copyBackToList();
  }, LV_EVENT_CLICKED, NULL);
}

static void copyTypePicked(float grams, const char*) {
  s_copy_core       = grams;
  s_copy_type_asked = true;
  buildCopyCard();
}

static void copyTypeWithout() {
  s_copy_core       = 0.0f;
  s_copy_type_asked = true;
  buildCopyCard();
}

void showCopyConfirmPopup(int template_spool_id, int template_filament_id,
                          const char* template_name,
                          float template_remaining, float template_initial, float template_spool_w,
                          const CopyLook* look) {
  closeCopyConfirmPopup();
  copy_template_spool_id    = template_spool_id;
  copy_template_filament_id = template_filament_id;
  copy_template_initial      = template_initial;
  copy_template_spool_w      = template_spool_w;
  strncpy(copy_template_name, template_name, sizeof(copy_template_name)-1);
  copy_template_name[sizeof(copy_template_name)-1] = '\0';
  copy_template_has_look = look != nullptr;
  if (look) copy_template_look = *look;
  s_copy_core       = 0.0f;
  s_copy_type_asked = false;
  // A new card starts from the template's price, as the read below forgets a
  // price typed for the one before.
  s_copy_price_typed = false;
  s_copy_price       = 0.0f;
  s_copy_price_tpl   = 0.0f;

  (void)template_remaining;   // the weight the template had is not the new spool's

  // Behind Bambuddy on Spoolman the template is read here, before the list of
  // types, whichever way it was chosen (Festlegung 15); the loop stands still
  // for it, as for the read of a row out of the cache (copyRowRefresh()). A
  // template that cannot be read gets that read's hint, and the way back is
  // Cancel's: no card for a copy that would create nothing (Festlegung 8).
  s_copy_bbs = spoolTypesActive();
  // A card outside it starts with nothing read: a BBS card closed from outside
  // the flow left its template and a typed price behind, and a create for the
  // same template would reuse them once the mode is back.
  if (!s_copy_bbs) copyBbsClear();
  if (s_copy_bbs && !copyBbsLoad(template_spool_id)) {
    copyBackToList();
    showInfoPopup(STR_SERVER_DOWN_TITLE, STR_SERVER_DOWN_TEXT, INFO_WARN);
    return;
  }
  // Behind Spoolman the copy starts from its filament's nominal weight, not
  // from a label the template had raised (copyRulesNominal). Set here, once,
  // so line 3 of the card and the create read the same number.
  if (s_copy_bbs) {
    copy_template_initial = copyBbsNominal(template_spool_id, template_initial);
    if (copy_template_initial != template_initial)
      logSDf("Copy: spool %d starts from its filament's %.0f g, not the label's %.0f g",
             template_spool_id, copy_template_initial, template_initial);
  }

  // The list only with Bambuddy on Spoolman and a spool on the pad: an empty
  // pad (or no number) has no net to take a type off (L22).
  if (!spoolTypesActive() || !(spoolExtrasBareReading() > NEW_SPOOL_TARE_MIN_G)) {
    buildCopyCard();
    return;
  }
  SpoolTypeListReq req = {};
  req.mode = STL_VENDOR;
  utf8Cut(look ? look->vendor : "", sizeof(req.vendor) - 1, req.vendor, sizeof(req.vendor));
  req.preselect_g = template_spool_w;
  req.skip_text   = STR_SPT_WITHOUT;
  showSpoolTypeList(req, copyTypePicked, copyTypeWithout, copyBackToList);
}
