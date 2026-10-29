#include "ui/tag_create_popup.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <lvgl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// backend_api.h brings ArduinoJson, whose templates have a parameter T:
// before lang.h, which defines T().
#include "services/backend_api.h"
#include "ui/spoolman_lookup.h"
#include "app/app_state.h"
#include "app/backend_switch.h"
#include "app_config.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/density_rules.h"
#include "services/no_tag_create_rules.h"
#include "services/server_reach.h"
#include "services/spool_cache.h"
#include "services/spool_extras.h"
#include "services/spool_start.h"
#include "services/spool_type_rules.h"
#include "services/tag_create.h"
#include "services/tag_db_match.h"
#include "services/tag_field.h"
#include "services/user_options.h"
#include "ui/batch_create.h"
#include "ui/main_screen_helpers.h"
#include "ui/no_tag_create.h"
#include "ui/price_pad.h"
#include "ui/sort_twin_popup.h"
#include "ui/spool_flow_internal.h"
#include "ui/spool_type_list.h"
#include "ui/tag_db_diff_popup.h"
#include "ui/tare_entry.h"
#include "ui/theme.h"
#include "ui/ui_common.h"

// The lines of the card, as on the copy confirmation.
#define TCP_LINE_H   26
#define TCP_SWATCH   18
#define TCP_GAP       8
// The answer row: create and cancel on either side, the price between them.
#define TCP_PRICE_W  80
#define TCP_ANSWER_W ((UI_POPUP_W - 2 * UI_CARD_ROW_X - 2 * TCP_GAP - TCP_PRICE_W) / 2)

// What the loop is to do for the card on its next pass.
enum TcpJob : uint8_t { TCP_IDLE = 0, TCP_PLAN, TCP_CREATE };

static lv_obj_t*       s_scr    = nullptr;
static lv_obj_t*       s_title  = nullptr;
static lv_obj_t*       s_status = nullptr;
static lv_obj_t*       s_btn_ok = nullptr;
static lv_obj_t*       s_lbl_price = nullptr;
static lv_obj_t*       s_swatch = nullptr;
static lv_obj_t*       s_meta   = nullptr;
// Where the tag and the database entry for its filament disagree.
static TagDbDiff       s_diffs[TAG_DB_DIFFS_MAX];
static int             s_diff_n = 0;
// What the spool cost, 0 while none was typed in.
static float           s_price  = 0.0f;
static TcpJob          s_job    = TCP_IDLE;
static TagCreateInput  s_in;
// The input came from the database picker, not from the tag: its colours are
// drawn from the input, the tag on the pad may have none.
static bool            s_picked = false;
static TagFilamentPlan s_plan;
static int             s_label_weight = 0;
// Cancel hides the card from its own callback; the loop deletes it.
static bool            s_close_pending = false;
// The plan is asked again before a create when it may no longer hold: after
// a failed try that left no filament behind, so a vendor made on the way, or
// a filament the server made although its answer was lost, is found rather
// than made twice; and after a switch of backend or host.
static bool            s_plan_stale = false;
static uint32_t        s_plan_gen   = 0;
// What was answered to "same colour under another name", and for which
// filament. Kept for the card: a plan asked again before the create finds the
// same twin and applies it, instead of asking twice.
enum TcpTwin : uint8_t { TCP_TWIN_OPEN = 0, TCP_TWIN_NEW, TCP_TWIN_TAKE };
static TcpTwin         s_twin_answer = TCP_TWIN_OPEN;
static int             s_twin_id     = 0;

lv_obj_t* tagCreatePopupScreen() { return s_scr; }

bool tagCreateOffered() {
  TagCreateInput in;
  return backendCanCreateFromTag() && tagCreateInputFromTag(&in);
}

void tagCreateEntryTap(lv_event_t*) {
  logSD("BTN: New from tag");
  newtag_open_pending = true;
}

void closeTagCreatePopup() {
  closePricePad();
  closeTagDbDiffPopup();
  closeSortTwinPopup();
  releaseScreen(&s_scr);
  s_title = nullptr;
  s_status = nullptr;
  s_btn_ok = nullptr;
  s_lbl_price = nullptr;
  s_swatch = nullptr;
  s_meta = nullptr;
  s_job = TCP_IDLE;
}

// ------------------------------------------------------------
//  Weights
// ------------------------------------------------------------

// The empty spool labelWeight() guesses the nominal size with: a spool type
// chosen at the scale for this spool (BBS, design note L20; for a Bambu tag
// the list asked in front of the card, V33), else the entry's or the tag's.
static float cardCore() {
  return (float)(s_in.spool_type_g > 0 ? s_in.spool_type_g : s_in.spool_weight_g);
}

// What the new spool starts with, off the pad as it stands (spool_start.h),
// weighed against the empty spool the inventory has on file for it. The plan
// brings that in, so the line is refreshed once the plan is. A spool type
// chosen for this spool comes first (tagCreateTare()). The reading is the one
// without the parts on the spool (spool_extras.h): they are neither the
// spool's nor the filament's, and the raw one would count them as filament.
static uint8_t s_tare_from = TARE_NONE;

// Wurzel: for a spool created without a tag the pad holds the wrapped roll that
// is being entered (a roll, a box, a bag of desiccant), nothing that is this
// spool's weight, and the start rule would file it as a measured net. Fix: in
// the flag the reading is 0 (noTagGross), whatever lies on the dish.
// NOT-TO-DO: reading it when it "looks like a spool"; a roll is no empty spool.
// See test_no_tag_weights.py.
static float startGross() {
  return noTagGross(noTagMode(), (g_scale_fitted && scale_ready) ? spoolExtrasBareReading() : 0.0f);
}

static SpoolStart startValues() {
  TareQuality q = TARE_Q_NONE;
  const int tare = tagCreateTare(s_in, s_plan, &s_tare_from, &q);
  return spoolStartValues(startGross(), tare, q, s_label_weight);
}

static bool tareInherited() { return s_tare_from == TARE_FILAMENT || s_tare_from == TARE_VENDOR; }

// The tag's own net weight; without it the nominal size nearest the reading.
// In the flag only what the input says, 0 when it says nothing: the guess
// below takes the reading for the spool, and the reading is the wrapped roll
// (see startGross()). A fill weight that stays 0 is asked for (fillAskDue()).
static int labelWeight() {
  if (noTagMode() || s_in.net_weight_g > 0) return s_in.net_weight_g;
  static const int choices[NEWTAG_LABEL_COUNT] = NEWTAG_LABEL_CHOICES;
  const float netto = spoolExtrasBareReading() - cardCore();
  int best = choices[NEWTAG_LABEL_COUNT - 1], best_diff = -1;
  for (int i = 0; i < NEWTAG_LABEL_COUNT; i++) {
    const int diff = (int)fabsf(netto - (float)choices[i]);
    if (best_diff < 0 || diff < best_diff) { best_diff = diff; best = choices[i]; }
  }
  return best;
}

// ------------------------------------------------------------
//  The status line and the create button
// ------------------------------------------------------------

// The text on the left answer button: "Create" (with the count of a create
// without a tag, batch_create.h), or "Density" / "Fill weight" while the card
// asks for one of them. The label is the button's first child (answerButton()).
// The id is kept, so that a new count can put the same text on again.
static int s_ok_text = STR_COPY_CARD_CREATE;
static void setOkText(int text_id) {
  s_ok_text = text_id;
  if (!s_btn_ok) return;
  lv_obj_t* lbl = lv_obj_get_child(s_btn_ok, 0);
  char caption[32];
  if (lbl) lv_label_set_text(lbl, text_id == STR_COPY_CARD_CREATE ? batchCreateCaption(caption, sizeof(caption)) : T(text_id));
}

// button_free: the left button can be pressed, to create or to ask for the
// density or the fill weight.
static void showStatus(const char* text, uint32_t color, bool button_free) {
  if (!s_status) return;
  lv_label_set_text(s_status, text);
  lv_obj_set_style_text_color(s_status, lv_color_hex(color), 0);
  // Every line is one body line; the two hints set their own after this.
  lv_obj_set_style_text_font(s_status, UI_FONT_BODY, 0);
  if (!s_btn_ok) return;
  // Every line but the two hints is about creating, so the button goes back to
  // "Create" whenever the status changes; showAsk() puts its text on after this.
  setOkText(STR_COPY_CARD_CREATE);
  if (button_free) lv_obj_clear_state(s_btn_ok, LV_STATE_DISABLED);
  else             lv_obj_add_state(s_btn_ok, LV_STATE_DISABLED);
}

// What the create button can act on.
static bool planCanCreate(TagFilamentState state) {
  return state == TFS_FOUND || state == TFS_CREATE_DB ||
         state == TFS_CREATE_TAG || state == TFS_NOT_NEEDED;
}

// The card needs something the scale cannot supply (a density, V34; a fill
// weight, X): the hint is the line's text, and the left button asks for it
// instead of creating, free to be pressed. One line of the small font (the hint
// names the button, so it must not wrap away from it); the line ends above the
// answer row.
static void showAsk(int hint_id, int button_id) {
  showStatus(T(hint_id), UI_COL_WARN, true);
  if (s_status) lv_obj_set_style_text_font(s_status, UI_FONT_SMALL, 0);
  setOkText(button_id);
}

// No source knows a density (or the one typed was out of range).
static void showDensityAsk(int hint_id) { showAsk(hint_id, STR_DENSITY_BTN); }

// No tag and no fill weight (or the one typed was out of range).
static void showFillAsk(int hint_id) { showAsk(hint_id, STR_NOTAG_FILL_BTN); }

// Whether the card asks for the fill weight now (X): it is missing, and the
// plan would otherwise let the card create or ask for a density. A plan that
// failed, or waits for the catalog, says so and keeps its button; the question
// comes with the plan that works. The fill weight goes before the density, so
// with both missing the density is asked once the fill weight is kept.
static bool fillAskDue() {
  return noTagFillMissing(noTagMode(), s_label_weight) && (planCanCreate(s_plan.state) || s_plan.no_density);
}

static void showPlan() {
  char buf[96];
  // Before everything else the plan has to say, the density included.
  if (fillAskDue()) {
    showFillAsk(STR_NOTAG_FILL_ASK);
    return;
  }
  switch (s_plan.state) {
    case TFS_FOUND:
      snprintf(buf, sizeof(buf), T(STR_TAGNEW_FOUND), s_plan.filament_id);
      showStatus(buf, UI_COL_GOOD, true);
      break;
    case TFS_CREATE_DB:
      // A tag the database knows: the tag's values, the entry's for the rest.
      if (!s_picked && (s_plan.db_found || s_in.db_id[0]))
        showStatus(T(STR_TAGNEW_TAG_PLUS_DB), UI_COL_ACCENT, true);
      else
        showStatus(T(backendIsFilaMan() ? STR_TAGNEW_FROM_FDB : STR_TAGNEW_FROM_DB), UI_COL_ACCENT, true);
      break;
    case TFS_CREATE_TAG:
      // A sort typed in is a new filament whose colour finds it again.
      showStatus(T(s_in.typed ? STR_SORT_NEW_FILAMENT : STR_TAGNEW_FROM_TAG), UI_COL_ACCENT, true);
      break;
    case TFS_NOT_NEEDED:    showStatus(T(STR_TAGNEW_BAMBUDDY), UI_COL_INK_SOFT, true);      break;
    case TFS_NEEDS_CATALOG: showStatus(T(STR_TAGNEW_NEEDS_CATALOG), UI_COL_WARN, false);    break;
    default:
      if (s_plan.no_density) {
        // A filament cannot be made without a density: the card asks for one.
        showDensityAsk(STR_SORT_NO_DENSITY);
        break;
      }
      snprintf(buf, sizeof(buf), T(STR_TAGNEW_FAILED), s_plan.http_code);
      showStatus(buf, UI_COL_BAD_TEXT, false);
      break;
  }
}

// ------------------------------------------------------------
//  The card
// ------------------------------------------------------------

static lv_obj_t* buildBox() {
  s_scr = lv_obj_create(lv_scr_act());
  lv_obj_set_size(s_scr, LV_HOR_RES, LV_VER_RES);
  lv_obj_set_pos(s_scr, 0, 0);
  lv_obj_set_style_bg_color(s_scr, lv_color_hex(UI_COL_SCRIM), 0);
  lv_obj_set_style_bg_opa(s_scr, UI_OPA_SCRIM, 0);
  lv_obj_set_style_border_width(s_scr, 0, 0);
  lv_obj_set_style_radius(s_scr, 0, 0);
  lv_obj_set_style_pad_all(s_scr, 0, 0);
  lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* box = lv_obj_create(s_scr);
  lv_obj_set_size(box, UI_POPUP_W, UI_CARD_H);
  lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(box, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_border_color(box, lv_color_hex(UI_COL_POPUP_BORDER), 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_radius(box, UI_RADIUS_BOX, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  return box;
}

// The title. A sort typed in is a new filament, "Neue Sorte anlegen?",
// unless it is one the inventory has: picked from the inventory's colours
// (known_filament_id) or taken as the twin of a colour typed. Then, as for a
// database entry, a new spool; a tag's own title for a tag.
static void refreshTitle() {
  if (!s_title) return;
  int id = s_picked ? STR_COPY_CONFIRM_TITLE : STR_NEWTAG_TITLE;
  if (s_in.typed && s_in.known_filament_id <= 0 && s_twin_answer != TCP_TWIN_TAKE) id = STR_SORT_CARD_TITLE;
  lv_label_set_text(s_title, T(id));
}

static void buildHead(lv_obj_t* box) {
  lv_obj_t* icon = lv_label_create(box);
  lv_label_set_text(icon, LV_SYMBOL_PLUS);
  lv_obj_set_style_text_color(icon, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_text_font(icon, UI_FONT_ICON, 0);
  lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, UI_CARD_ICON_Y);

  s_title = lv_label_create(box);
  lv_obj_set_style_text_color(s_title, lv_color_hex(UI_COL_INK), 0);
  lv_obj_set_style_text_font(s_title, UI_FONT_HEADLINE, 0);
  lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(s_title, UI_POPUP_W - UI_CARD_TEXT_PAD);
  lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, UI_CARD_TITLE_Y);
  refreshTitle();
}

// Line 1: the colour and the product, as one centred group.
static void buildIdentity(lv_obj_t* box) {
  lv_obj_t* row = lv_obj_create(box);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, UI_POPUP_W - UI_CARD_TEXT_PAD, TCP_LINE_H);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, UI_CARD_TEXT_Y - 4);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, TCP_GAP, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* sw = lv_obj_create(row);
  lv_obj_set_size(sw, TCP_SWATCH, TCP_SWATCH);
  lv_obj_set_style_radius(sw, UI_RADIUS_INPUT, 0);
  lv_obj_set_style_border_width(sw, 1, 0);
  lv_obj_set_style_border_color(sw, lv_color_hex(UI_COL_POPUP_BORDER), 0);
  lv_obj_set_style_pad_all(sw, 0, 0);
  lv_obj_clear_flag(sw, LV_OBJ_FLAG_SCROLLABLE);
  s_swatch = sw;
  if (s_picked) {
    char rgba[10];
    snprintf(rgba, sizeof(rgba), "%s", s_in.clear ? "FFFFFF00" : s_in.color_hex);
    swatchPaintHex(sw, rgba);
  } else {
    swatchPaint(sw, g_tag.color);
  }
  // A gradient or dual colour spool: its first two colours, the way the tag
  // view draws it.
  if (s_in.color_count >= 2) {
    lv_obj_set_style_bg_grad_color(sw, lv_color_hex(strtoul(s_in.colors_hex[1], nullptr, 16)), 0);
    lv_obj_set_style_bg_grad_dir(sw, LV_GRAD_DIR_VER, 0);
  }

  char name[80];
  joinMaterialName(s_in.product, s_in.color_name, name, sizeof(name));
  lv_obj_t* lbl = lv_label_create(row);
  lv_label_set_text(lbl, name);
  lv_obj_set_style_text_color(lbl, lv_color_hex(UI_COL_INK), 0);
  lv_obj_set_style_text_font(lbl, UI_FONT_TITLE, 0);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_width(lbl, UI_POPUP_W - UI_CARD_TEXT_PAD - TCP_SWATCH - TCP_GAP, 0);
}

static lv_obj_t* textLine(lv_obj_t* box, const lv_font_t* font, uint32_t color, int y) {
  lv_obj_t* lbl = lv_label_create(box);
  lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(lbl, font, 0);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(lbl, UI_POPUP_W - UI_CARD_TEXT_PAD);
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, y);
  return lbl;
}

// Line 2: whose it is, the article number, and what the spool starts with.
// A reading that could not be taken as the spool's says why instead, and
// stands alone on the line: the hint matters more than the maker's name.
static void refreshMeta() {
  if (!s_meta) return;
  const SpoolStart start = startValues();
  char weight[64], meta[112];
  spoolStartText(start, tareInherited(), weight, sizeof(weight));
  const bool plain = start.verdict == START_FULL || start.verdict == START_PARTIAL;
  if (!plain)               snprintf(meta, sizeof(meta), "%s", weight);
  else if (s_in.article[0]) snprintf(meta, sizeof(meta), "%s | %s | %s", s_in.vendor, s_in.article, weight);
  else                      snprintf(meta, sizeof(meta), "%s | %s", s_in.vendor, weight);
  lv_label_set_text(s_meta, meta);
}

// Line 3: the filament, filled in once the lookup has answered.
static void buildLines(lv_obj_t* box) {
  s_meta = textLine(box, UI_FONT_SMALL, UI_COL_INK_SOFT, UI_CARD_TEXT_Y - 4 + TCP_LINE_H + 4);
  refreshMeta();

  s_status = textLine(box, UI_FONT_BODY, UI_COL_INK_SOFT, UI_CARD_TEXT_Y - 4 + 2 * TCP_LINE_H + 8);
  lv_label_set_text(s_status, T(STR_TAGNEW_SEARCHING));
}

static lv_obj_t* answerButton(lv_obj_t* box, int x, int w, bool ok, int text_id) {
  lv_obj_t* btn = lv_btn_create(box);
  lv_obj_set_size(btn, w, UI_POPUP_BTN_H);
  lv_obj_set_pos(btn, x, UI_CARD_ROW_Y);
  lv_obj_set_style_bg_color(btn, lv_color_hex(ok ? UI_COL_OK_BG : UI_COL_BAD_BG), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(ok ? UI_COL_OK_BG_PRESSED : UI_COL_BAD_BG_PRESSED),
                            LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COL_DISABLED_BG), LV_STATE_DISABLED);
  lv_obj_set_style_radius(btn, UI_RADIUS_BTN, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_t* lbl = lv_label_create(btn);
  lv_label_set_text(lbl, T(text_id));
  lv_obj_set_style_text_color(lbl, lv_color_hex(ok ? UI_COL_OK_TEXT : UI_COL_BAD_TEXT), 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(UI_COL_DISABLED_TEXT), LV_STATE_DISABLED);
  lv_obj_set_style_text_font(lbl, UI_FONT_TITLE, 0);
  lv_obj_center(lbl);
  return btn;
}

// The price button shows what was typed in, or that there is none yet.
static void refreshPrice() {
  if (!s_lbl_price) return;
  if (s_price <= 0.0f) { lv_label_set_text(s_lbl_price, T(STR_TAGNEW_PRICE_BTN)); return; }
  char buf[16];
  snprintf(buf, sizeof(buf), "%.2f", s_price);
  lv_label_set_text(s_lbl_price, buf);
}

static void onPrice(float price) {
  s_price = price;
  refreshPrice();
}

static void buildPriceButton(lv_obj_t* box) {
  lv_obj_t* btn = lv_btn_create(box);
  lv_obj_set_size(btn, TCP_PRICE_W, UI_POPUP_BTN_H);
  lv_obj_set_pos(btn, UI_CARD_ROW_X + TCP_ANSWER_W + TCP_GAP, UI_CARD_ROW_Y);
  lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COL_ROW), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COL_ROW_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(UI_COL_LINE), 0);
  lv_obj_set_style_radius(btn, UI_RADIUS_BTN, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_add_event_cb(btn, [](lv_event_t*) {
    if (s_job != TCP_IDLE) return;
    logSD("BTN: TagCreate -> Price");
    showPricePad(s_price, onPrice);
  }, LV_EVENT_CLICKED, NULL);
  s_lbl_price = lv_label_create(btn);
  lv_obj_set_style_text_color(s_lbl_price, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_style_text_font(s_lbl_price, UI_FONT_SMALL, 0);
  lv_obj_center(s_lbl_price);
  refreshPrice();
}

// The answer of the price pad when it asked for a density (V34), handed on from
// the loop (pricePadTick()), where the plan may be asked. An emptied field
// types nothing and Cancel never gets here, so the card stays as it was. A
// value outside the plausible range (a point in the wrong place, 11.6 for 1.16)
// is not taken: the line says the range and the button keeps asking.
static void onDensity(float density) {
  if (density <= 0.0f) return;
  if (!densityPlausible(density)) {
    logSDf("TagCreate: density %.2f is out of range, not taken", density);
    showDensityAsk(STR_DENSITY_RANGE);
    return;
  }
  // The density stays in the input, not in the plan: the plan is made again
  // from the input (here, and before a create that finds it stale), and the
  // typed density counts last there (spoolman_filament.cpp), so one that a
  // source knows by now still wins.
  s_in.density_typed = density;
  logSDf("TagCreate: density %.2f typed at the scale, plan asked again", density);
  showStatus(T(STR_TAGNEW_SEARCHING), UI_COL_INK_SOFT, false);
  s_plan_stale = true;
  s_job = TCP_PLAN;
}

// The answer of the price pad when it asked for the fill weight (X, no tag),
// handed on from the loop (pricePadTick()). Like the density's: an emptied
// field types nothing and Cancel never gets here, so the card stays as it was;
// a value outside NO_TAG_FILL_MIN_G to NO_TAG_FILL_MAX_G is not taken, the line
// says the range and the button keeps asking. A valid one is the card's label
// weight from here on (rounded to whole grams, as the backends take it). The
// plan is asked again before the create: it keys on the weight (a database
// entry of another weight no longer fits, nor the density found for it).
static void onFill(float grams) {
  if (grams <= 0.0f) return;
  if (!noTagFillPlausible(grams)) {
    logSDf("TagCreate: fill weight %.0f g is out of range, not taken", grams);
    showFillAsk(STR_NOTAG_FILL_RANGE);
    return;
  }
  s_in.net_weight_g = (int)lroundf(grams);
  s_label_weight = labelWeight();
  s_plan_stale = true;
  logSDf("TagCreate: fill weight %d g typed at the scale", s_in.net_weight_g);
  refreshMeta();
  showPlan();
}

static void buildAnswers(lv_obj_t* box) {
  s_btn_ok = answerButton(box, UI_CARD_ROW_X, TCP_ANSWER_W, true, STR_COPY_CARD_CREATE);
  setOkText(STR_COPY_CARD_CREATE);                 // with the count, from the first paint
  lv_obj_add_state(s_btn_ok, LV_STATE_DISABLED);   // until the lookup has answered
  lv_obj_add_event_cb(s_btn_ok, [](lv_event_t*) {
    if (s_job != TCP_IDLE) return;
    // No tag and no fill weight: the button asks for it on the price pad
    // instead of creating, ahead of the density, and the answer comes back
    // through onFill().
    if (fillAskDue()) {
      logSD("BTN: TagCreate -> Fill weight");
      showPricePadInfo((float)s_in.net_weight_g, STR_NOTAG_FILL_TITLE, STR_NOTAG_FILL_INFO, onFill);
      return;
    }
    // No density known: the button asks for it on the price pad instead of
    // creating, and the answer comes back through onDensity().
    if (s_plan.no_density) {
      logSD("BTN: TagCreate -> Density");
      // The pad's grey line says what empty means and the range: the one for a
      // price talks about the currency.
      showPricePadInfo(s_in.density_typed, STR_DENSITY_PAD_TITLE, STR_DENSITY_PAD_INFO, onDensity);
      return;
    }
    logSD("BTN: TagCreate -> Create");
    showStatus(T(STR_TAGNEW_CREATING), UI_COL_INK_SOFT, false);
    s_job = TCP_CREATE;
  }, LV_EVENT_CLICKED, NULL);

  buildPriceButton(box);

  lv_obj_t* no = answerButton(box, UI_POPUP_W - TCP_ANSWER_W - UI_CARD_ROW_X, TCP_ANSWER_W,
                              false, STR_CANCEL);
  styleLeaveButton(no);
  lv_obj_add_event_cb(no, [](lv_event_t*) {
    logSD("BTN: TagCreate -> Cancel");
    // Hidden here, deleted by the loop: this is the button's own callback.
    if (s_scr) lv_obj_add_flag(s_scr, LV_OBJ_FLAG_HIDDEN);
    s_job = TCP_IDLE;
    s_close_pending = true;
  }, LV_EVENT_CLICKED, NULL);
}

static void buildCard() {
  s_label_weight = labelWeight();
  s_price = 0.0f;
  s_twin_answer = TCP_TWIN_OPEN;
  s_twin_id = 0;
  tagFilamentPlanClear(&s_plan);

  lv_obj_t* box = buildBox();
  buildHead(box);
  buildIdentity(box);
  buildLines(box);
  buildAnswers(box);
  // Without a tag, the count at the top right. A new one only puts the text on
  // the left button again: after a lost answer it stays disabled.
  if (noTagMode()) batchCountChip(box, []() { setOkText(s_ok_text); });
  s_job = TCP_PLAN;
}

// ---- the spool type, asked first (V33) ----

// A roll with a Bambu tag is made on Bambu's fixed 250 g core, but a refill
// sits on another spool and was weighed against 250 g, so the card for a Bambu
// tag asks the type of the spool first, when one is on the pad (the rule is
// spoolTypeTagAsk()). The list comes from Bambuddy's own catalog, with or
// without Spoolman behind it (any_inventory). The vendor's types come first
// ("show all" is offered; all types are shown when the vendor has none, the
// list's own behaviour), "without type" is the way out; no measured weight, so
// no "take measured" and no new type. The input waits in s_in while the list
// is up.
// NOT-TO-DO: dbPickTypeAsk() (db_pick_core_popup.cpp). It goes on into
// showTagCreatePopupFor(), which makes the card a database pick (title, colour
// source, status line), and that is not what a tag's card is.

// Called from the loop (spoolTypeListTick()), where the card may be built. A
// card still standing is closed first, as showTagCreatePopup() does.
static void tagTypeGoOn() {
  closeTagCreatePopup();
  buildCard();
}

// Both answers (a type, or "without type") travel with the input to the card,
// whose create marks the new spool for the first weighing
// (tareEntryFirstWeighDone()).
static void tagTypePicked(float grams, const char* name) {
  s_in.spool_type_g     = spoolTypeGrams(grams);
  s_in.spool_type_asked = true;
  logSDf("TagCreate: spool type '%s' %d g for this spool alone", name ? name : "", s_in.spool_type_g);
  tagTypeGoOn();
}

// Without a type the card goes on exactly as it did before the list: the
// input keeps Bambu's core as the maker's figure, and only the flag is set so
// that the first weighing does not ask again.
static void tagTypeWithout() {
  s_in.spool_type_g     = 0;
  s_in.spool_type_asked = true;
  logSD("TagCreate: without a spool type, the card goes on as before");
  tagTypeGoOn();
}

// Back leaves the whole create: no card, nothing made. A function of its own,
// not null, which the list would take as "without type".
static void tagTypeBack() {
  logSD("TagCreate: spool type list left, nothing created");
}

// True when the list was opened (the card follows the answer, or does not
// follow at all after Back). The reading is the one the card takes, so a scale
// that is not fitted or not ready asks nothing.
static bool tagTypeAsk() {
  const float bare = startGross();
  if (!spoolTypeTagAsk(tagIsBambu(s_in.link_id), backendIsBamBuddy(), bare, s_in.spool_type_g))
    return false;
  SpoolTypeListReq req = {};
  req.mode = STL_VENDOR;
  utf8Cut(s_in.vendor, sizeof(req.vendor) - 1, req.vendor, sizeof(req.vendor));
  req.preselect_g   = (float)s_in.spool_weight_g;
  req.skip_text     = STR_SPT_WITHOUT;
  req.any_inventory = true;
  logSDf("TagCreate: Bambu tag with %.0f g on the pad, spool type asked first", bare);
  showSpoolTypeList(req, tagTypePicked, tagTypeWithout, tagTypeBack);
  return true;
}

void showTagCreatePopup() {
  logSD("SHOW: TagCreatePopup");
  // The card for the tag on the pad is not a create without a tag; a flag left
  // on by a cancelled one would drop the tag's link. showTagCreatePopupFor()
  // keeps it: the database and the sort entry come in with the flag on.
  noTagModeSet(false);
  closeTagCreatePopup();
  if (!tagCreateInputFromTag(&s_in)) {
    logSD("TagCreate: no tag to create from");
    return;
  }
  s_picked = false;
  if (tagTypeAsk()) return;
  buildCard();
}

void showTagCreatePopupFor(const TagCreateInput& in) {
  logSD("SHOW: TagCreatePopup (picked)");
  closeTagCreatePopup();
  s_in = in;
  s_picked = true;
  buildCard();
}

// ------------------------------------------------------------
//  The loop's part
// ------------------------------------------------------------

// The user's answer to the differences: the database's values, or the tag's
// as they stand.
static void onDiffAnswer(bool take_db) {
  logSDf("TagCreate: %d difference(s), %s", s_diff_n, take_db ? "database taken" : "tag kept");
  if (!take_db) return;
  for (int i = 0; i < s_diff_n; i++) {
    tagDbTake(&s_in, s_diffs[i]);
    if (s_diffs[i].field == TDF_COLOR && s_swatch) swatchPaintHex(s_swatch, s_in.color_hex);
  }
  s_label_weight = labelWeight();
  refreshMeta();
}

// The database knows the tag's filament: what the tag lacks comes from the
// entry, and where both say something different, the user is asked. The
// tag stays as it is until then (tag_db_match.h).
static void mergeDbEntry() {
  s_diff_n = tagDbCompare(s_in, s_plan.db, s_diffs, TAG_DB_DIFFS_MAX);
  tagDbFill(&s_in, s_plan.db);
  // The plan was made from the tag alone; the create asks it again with the
  // entry linked, so the backend writes what the card now holds.
  s_plan_stale = true;
  logSDf("TagCreate: database entry #%s merged, %d difference(s)", s_plan.db.id, s_diff_n);
  s_label_weight = labelWeight();
  refreshMeta();
  // The entry may bring the fill weight the line was still asking for.
  if (noTagMode()) showPlan();
  if (s_diff_n > 0) showTagDbDiffPopup(s_diffs, s_diff_n, onDiffAnswer);
}

// The twin taken: the plan is the found filament's from here on, with its
// own empty spool for the weights, and the card makes a new spool of it.
static void takeTwin() {
  s_plan.state = TFS_FOUND;
  s_plan.filament_id = s_plan.twin_id;
  s_plan.found_spool_weight_g = s_plan.twin_spool_weight_g;
  showPlan();
  refreshMeta();
  refreshTitle();
}

// The user's answer to the twin: that filament, or a new one as planned.
static void onTwinAnswer(bool take) {
  logSDf("TagCreate: twin #%d \"%s\", %s", s_plan.twin_id, s_plan.twin_name,
         take ? "taken" : "new one made");
  s_twin_answer = take ? TCP_TWIN_TAKE : TCP_TWIN_NEW;
  s_twin_id = s_plan.twin_id;
  if (take) takeTwin();
  else      showPlan();   // the create button is free again
}

// A sort typed in whose colour the inventory has under another name: the
// card asks, and holds the create button until it is answered. A plan asked
// again for the same twin applies the answer given before. A failed plan
// says nothing about the twin and leaves the answer alone.
static void settleTwin() {
  if (!s_in.typed) return;
  if (s_plan.state != TFS_CREATE_TAG && s_plan.state != TFS_CREATE_DB) return;
  if (s_plan.twin_id <= 0) {
    s_twin_answer = TCP_TWIN_OPEN;
    s_twin_id = 0;
    return;
  }
  if (s_twin_answer != TCP_TWIN_OPEN && s_twin_id == s_plan.twin_id) {
    logSDf("TagCreate: twin #%d, the answer given before applies", s_plan.twin_id);
    if (s_twin_answer == TCP_TWIN_TAKE) takeTwin();
    return;
  }
  s_twin_answer = TCP_TWIN_OPEN;
  if (s_btn_ok) lv_obj_add_state(s_btn_ok, LV_STATE_DISABLED);
  showSortTwinPopup(s_plan.twin_name, onTwinAnswer);
}

static void runPlan() {
  s_plan_stale = false;
  s_plan_gen   = backendGeneration();
  if (!wifi_ok) {
    // The plan is not asked, so it still holds the last answer's flags: a
    // no_density from before would show the density ask instead of the failure.
    s_plan.no_density = false;
    s_plan.state = TFS_FAILED;
    showPlan();
    return;
  }
  backendPlanTagFilament(s_in, &s_plan);
  serverReachNote(s_plan.state == TFS_FAILED ? s_plan.http_code : 200, true);
  showPlan();
  refreshMeta();   // the plan may have brought an empty spool weight
  if (s_plan.state == TFS_CREATE_DB && s_plan.db_found && !s_in.db_id[0]) mergeDbEntry();
  settleTwin();
  // A plan asked again may have found another twin, or none: the answer
  // given before no longer holds, and neither does its title.
  refreshTitle();
}

// The request reached the server but its answer did not come back, so the
// server may have done it: a read timeout, a dropped link, a proxy that gave
// up waiting. A refused connection or a 502 never got that far.
static bool answerLost(int code) {
  return code == HTTPC_ERROR_READ_TIMEOUT || code == HTTPC_ERROR_CONNECTION_LOST ||
         code == HTTP_CODE_GATEWAY_TIMEOUT;
}

static void runCreate() {
  // Without Wi-Fi the create would sit in the HTTP stack until its connect
  // timeout; the plan's own path says so on the card instead.
  if (!wifi_ok) { runPlan(); return; }
  if (s_plan_stale || s_plan_gen != backendGeneration()) {
    logSD("TagCreate: plan asked again before the create");
    runPlan();
    // showPlan() has said why; a new twin is asked before anything is made.
    if (!planCanCreate(s_plan.state) || sortTwinPopupOpen()) return;
  }
  TagCreateResult r;
  const SpoolStart start = startValues();
  logSDf("TagCreate: start %s gross=%.0f tare=%d (%s) label=%d -> initial=%d remaining=%.0f",
         spoolStartVerdictName(start.verdict), startGross(), start.tare_g,
         tareSourceName(s_tare_from), s_label_weight, start.initial_g, start.remaining_g);
  const TagSpoolValues values = { start.initial_g, start.remaining_g, s_price };
  const int code = serverReachNote(backendCreateFromTag(s_in, s_plan, values, &r), true);
  logSDf("TagCreate: HTTP %d, spool %d, filament %d, full %d g, price %.2f, link %s",
         code, r.spool_id, r.filament_id, start.initial_g, s_price, s_in.link_id);
  char buf[96];
  if ((code == 200 || code == 201) && r.spool_id > 0) {
    spoolCacheForget("spool created from a tag");
    // No tag: no link and no mark for the first weighing, see
    // noTagCreateFinish(). A price the server did not keep is still said, over
    // the green line, unless the Bambu chip hint stands on it (noTagYellowLineAllowed()).
    if (noTagMode()) {
      closeTagCreatePopup();
      noTagCreateFinish(r.spool_id, s_in.vendor);
      if (r.price_lost && noTagYellowLineAllowed(r.spool_id, "price not saved", 0)) statusMessageShow(T(STR_TAGNEW_PRICE_LOST), UI_COL_WARN);
      return;
    }
    // Our list asked this spool's type on the way here ("new from database",
    // or in front of the card for a Bambu tag, tagTypeAsk()): the first
    // weighing does not ask it again (tare_entry.h). A tag's input carries
    // it only from that list.
    if (s_in.spool_type_asked) tareEntryFirstWeighDone(r.spool_id);
    char link_id[sizeof(s_in.link_id)];
    memcpy(link_id, s_in.link_id, sizeof(link_id));
    const bool with_filament = r.filament_id > 0;
    const bool price_lost = r.price_lost;
    closeTagCreatePopup();
    // The spool exists either way; a tag that could not be bound has said so
    // on the status line, and that must stay readable.
    if (!finishCopyFlow(r.spool_id, link_id)) return;
    if (price_lost) statusMessageShow(T(STR_TAGNEW_PRICE_LOST), UI_COL_WARN);
    else statusMessageShow(T(with_filament ? STR_TAGNEW_OK_BOTH : STR_NEWTAG_OK), UI_COL_GOOD);
    return;
  }
  // The spool may exist without the tag's link. Another try would make a
  // second one, so the card stops here and the inventory has the answer.
  if (r.spool_sent && answerLost(code)) {
    showStatus(T(STR_TAGNEW_SPOOL_UNSURE), UI_COL_BAD_TEXT, false);
    return;
  }
  // A filament that was created stays; the next try uses it.
  if (r.filament_id > 0) {
    s_plan.state = TFS_FOUND;
    s_plan.filament_id = r.filament_id;
    snprintf(buf, sizeof(buf), T(STR_TAGNEW_FILAMENT_ONLY), r.filament_id, code);
  } else {
    s_plan_stale = true;
    snprintf(buf, sizeof(buf), T(STR_TAGNEW_CREATE_FAIL), code);
  }
  showStatus(buf, UI_COL_BAD_TEXT, true);
}

void tagCreatePopupTick() {
  pricePadTick();
  tagDbDiffPopupTick();
  sortTwinPopupTick();
  if (s_close_pending) {
    s_close_pending = false;
    closeTagCreatePopup();
    return;
  }
  if (!s_scr || s_job == TCP_IDLE) return;
  const TcpJob job = s_job;
  s_job = TCP_IDLE;
  // The card as it stands now, before the loop waits on the server.
  lv_refr_now(NULL);
  if (job == TCP_PLAN) runPlan();
  else                 runCreate();
}
