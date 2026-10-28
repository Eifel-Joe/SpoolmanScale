#include "weight_popup.h"

#include <Arduino.h>
#include <lvgl.h>
#include <cstdio>
#include <cstring>

#include "app/app_state.h"
#include "app/deferred_actions.h"
#include "app/id_spool.h"
#include "hardware/sd_logger.h"
#include "services/auto_weight_state.h"
#include "services/backend.h"
#include "services/backend_api.h"
#include "services/spool_extras.h"
#include "services/spool_types.h"
#include "confirm_popup.h"
#include "extras_chooser.h"
#include "tare_entry.h"
#include "weight_button.h"
#include "lang.h"
#include "theme.h"
#include "ui_common.h"

// Redesign of 26.10.2026 (mock-ups on the canvas "Gewicht-Popup Redesign").
// The popup it replaces gave seven buttons nearly the same weight; sending
// the weight, which is what it is opened for almost every time, was 14 px
// taller than archiving. Now one large button sends, the two switches that
// change what it sends sit under it, and the rare things move to Options.
//
// One screen's coordinates, inside the box's content area (456 wide).
#define WP_BOX_W        460
#define WP_X             10
#define WP_INNER_W      436
#define WP_PRIMARY_Y     54
#define WP_PRIMARY_H     76
#define WP_PRIMARY_SUB_DY 20
#define WP_ROW_Y0       138
#define WP_ROW_H         46
#define WP_ROW_STEP      52
#define WP_SWITCH_ROWS_MAX 2  // bag, automatic sending
#define WP_BOTTOM_H      46
#define WP_BOX_EXTRA     12    // the margin under the last row, and the border
#define WP_ARCHIVE_W    120
#define WP_GAP            8
#define WP_OPT_ROW_Y0    40
#define WP_OPT_ROW_H     58
#define WP_OPT_ROW_STEP  64
// The parts chip, top right beside both header lines (ours, see
// wpBuildPartsChip()). 4 + 44 ends above the large button at 54.
#define WP_CHIP_W        84
#define WP_CHIP_H        44
#define WP_CHIP_Y         4

static lv_obj_t *s_popup = nullptr;   // level 1
static lv_obj_t *s_opts  = nullptr;   // level 2, Options

// What level 1 repaints in place when a switch changes. Rebuilding would
// delete the switch from inside its own callback.
struct WpView {
  lv_obj_t *line1, *change, *diff;
  lv_obj_t *primary, *main_lbl, *main_sub;
  lv_obj_t *bag_row, *bag_sub, *bag_state;
  lv_obj_t *auto_row, *auto_state;
};
static WpView s_v = {};

// The reading when the popup opened. Every number on it and every write it
// makes comes from this one value, so what the user read is what lands.
// Fork: the raw pad reading carries the parts on the spool, a clip or a tag
// holder, which are neither filament nor the empty spool the backend knows,
// and every number here would send them as filament. So this is the reading
// without them (spoolExtrasBareReading()): the large button, Full, the bag
// row and risen() leave them out, as the automatic sending does
// (auto_send_rules.h, whose rise test risen() repeats). A change on the parts
// chip opens the popup again with a fresh reading.
// NOT-TO-DO: no taking the parts off in newG() alone: the gross handed to
// confirmQueueWeight() for the AMS note would still carry them.
static float s_gross_g = 0.0f;
// "Weighed in the bag", off every time the popup opens: a spool in use is
// weighed bare almost always (Nikolai, 26.10.2026). Fork: not on the way back
// from the parts chooser, see wpReopenAfterParts().
static bool  s_bag_on  = false;
// The spool the parts chooser was opened for from the chip.
static int   s_parts_for = 0;

static float bagG() { return s_bag_on ? bag_weight_g : 0.0f; }

static float newG() {
  const float g = s_gross_g - (float)sm_spool_weight - bagG();
  return g < 0.0f ? 0.0f : g;
}

// The same test the automatic weighing applies before it writes.
static bool risen() {
  return !sm_archived && newG() - sm_remaining > AUTO_WEIGHT_RISE_G;
}

void closeWeightPopups() {
  releaseScreen(&s_popup);
  releaseScreen(&s_opts);
  s_v = {};
}

// The parts chooser opened from the chip stands in for the popup it came
// from and opens it again: the automatic weighing waits while it is up, as
// for the popup.
bool isWeightPopupOpen() {
  return s_popup != nullptr || s_opts != nullptr || isExtrasChooserOpen();
}

// ---------------------------------------------------------------------------
//  Building blocks
// ---------------------------------------------------------------------------

static lv_obj_t* wpLabel(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t col) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
  return l;
}

static lv_obj_t* wpButton(lv_obj_t *box, int x, int y, int w, int h) {
  lv_obj_t *b = lv_btn_create(box);
  lv_obj_set_size(b, w, h);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_ROW), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_ROW_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(UI_COL_ROW_PRESSED), 0);
  lv_obj_set_style_radius(b, UI_RADIUS_BTN, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  return b;
}

// A switch in the look of the settings lists: the whole row is the button,
// ON or OFF on its right, the border in the accent while it is on. No
// slider: one would invite a tap on itself while the row is what answers.
struct WpSwitch { lv_obj_t *row, *sub, *state; };

static WpSwitch wpSwitchRow(lv_obj_t *box, int y, const char *icon, int title_id) {
  WpSwitch s;
  s.row = wpButton(box, WP_X, y, WP_INNER_W, WP_ROW_H);
  lv_obj_align(wpLabel(s.row, icon, UI_FONT_BODY, UI_COL_ACCENT), LV_ALIGN_LEFT_MID, 14, 0);
  lv_obj_align(wpLabel(s.row, T(title_id), UI_FONT_BODY, UI_COL_INK), LV_ALIGN_LEFT_MID, 44, -9);
  s.sub = wpLabel(s.row, "", UI_FONT_CAPTION, UI_COL_CAPTION);
  lv_obj_align(s.sub, LV_ALIGN_LEFT_MID, 44, 11);
  s.state = wpLabel(s.row, "", UI_FONT_SMALL, UI_COL_CAPTION);
  lv_obj_align(s.state, LV_ALIGN_RIGHT_MID, -14, 0);
  return s;
}

// line: 0 for the usual border, or a colour that overrides it.
static void wpPaintSwitch(lv_obj_t *row, lv_obj_t *state, bool on, uint32_t line) {
  lv_label_set_text(state, T(on ? STR_ON : STR_OFF));
  lv_obj_set_style_text_color(state, lv_color_hex(on ? UI_COL_ACCENT : UI_COL_CAPTION), 0);
  if (!line) line = on ? UI_COL_ACCENT : UI_COL_ROW_PRESSED;
  lv_obj_set_style_border_color(row, lv_color_hex(line), 0);
}

// ---------------------------------------------------------------------------
//  Level 1: painting
// ---------------------------------------------------------------------------

static void wpPaintHeader(float g, bool rise) {
  char buf[96];
  if (rise) {
    snprintf(buf, sizeof(buf), T(STR_WP_RISE_LINE), backendName());
  } else {
    const char *tail = sm_archived ? T(STR_ARCHIVED) : sm_vendor_g;
    snprintf(buf, sizeof(buf), tail[0] ? "#%d  %s - %s" : "#%d  %s", sm_id, sm_filament_name, tail);
  }
  lv_label_set_text(s_v.line1, buf);
  lv_obj_set_style_text_color(s_v.line1, lv_color_hex(rise ? UI_COL_WARN
                              : sm_archived ? UI_COL_ARCHIVE_TEXT : UI_COL_INK_SOFT), 0);

  snprintf(buf, sizeof(buf), "%s  %.0f g   " LV_SYMBOL_RIGHT "   %.0f g",
           backendName(), sm_remaining, g);
  lv_label_set_text(s_v.change, buf);
  snprintf(buf, sizeof(buf), "%+.0f g", g - sm_remaining);
  lv_label_set_text(s_v.diff, sm_archived ? "" : buf);
  lv_obj_set_style_text_color(s_v.diff, lv_color_hex(rise ? UI_COL_WARN : UI_COL_INK_SOFT), 0);
}

static void wpPaintPrimary(float g, bool rise) {
  char num[48], buf[64], sub[64] = "";
  uint32_t bg = UI_COL_OK_BG, line = UI_COL_OK_BG_PRESSED, ink = UI_COL_OK_TEXT;
  if (sm_archived) {
    snprintf(buf, sizeof(buf), LV_SYMBOL_REFRESH "  %s", T(STR_WP_REACTIVATE));
    snprintf(sub, sizeof(sub), T(STR_WP_REACT_SUB), g);
    bg = UI_COL_GO_BG; line = UI_COL_ACCENT; ink = UI_COL_ACCENT;
  } else if (rise) {
    // Still possible, but no longer the obvious thing to do.
    snprintf(buf, sizeof(buf), T(STR_WP_SEND_ANYWAY), g);
    bg = UI_COL_ROW; line = UI_COL_LINE; ink = UI_COL_INK_2;
  } else {
    snprintf(num, sizeof(num), T(STR_WP_SEND), g);
    snprintf(buf, sizeof(buf), LV_SYMBOL_OK "  %s", num);
  }
  if (!sm_archived && s_bag_on) snprintf(sub, sizeof(sub), T(STR_WP_BAG_MINUS), g + bag_weight_g, bag_weight_g);

  lv_obj_set_style_bg_color(s_v.primary, lv_color_hex(bg), 0);
  lv_obj_set_style_border_color(s_v.primary, lv_color_hex(line), 0);
  lv_label_set_text(s_v.main_lbl, buf);
  lv_obj_set_style_text_color(s_v.main_lbl, lv_color_hex(ink), 0);
  lv_label_set_text(s_v.main_sub, sub);
  const bool has_sub = sub[0] != '\0';
  lv_obj_align(s_v.main_lbl, LV_ALIGN_CENTER, 0, has_sub ? -WP_PRIMARY_SUB_DY / 2 : 0);
  lv_obj_align(s_v.main_sub, LV_ALIGN_CENTER, 0, WP_PRIMARY_SUB_DY);
}

// When the spool reads heavier than stored and the switch is off, the row
// says what the bag would make of it: the likeliest explanation, one tap away.
static void wpPaintBag(float g, bool rise) {
  if (!s_v.bag_row) return;
  const bool hint = rise && !s_bag_on;
  char buf[48];
  if (hint) {
    const float with_bag = g - bag_weight_g;
    snprintf(buf, sizeof(buf), T(STR_WP_BAG_HINT), with_bag < 0.0f ? 0.0f : with_bag);
  } else {
    snprintf(buf, sizeof(buf), T(STR_WP_BAG_SUB), bag_weight_g);
  }
  lv_label_set_text(s_v.bag_sub, buf);
  lv_obj_set_style_text_color(s_v.bag_sub, lv_color_hex(hint ? UI_COL_WARN : UI_COL_CAPTION), 0);
  wpPaintSwitch(s_v.bag_row, s_v.bag_state, s_bag_on, hint ? UI_COL_WARN : 0);
}

static void wpRefresh() {
  if (!s_popup) return;
  const float g = newG();
  const bool rise = risen();
  wpPaintHeader(g, rise);
  wpPaintPrimary(g, rise);
  wpPaintBag(g, rise);
  if (s_v.auto_row) wpPaintSwitch(s_v.auto_row, s_v.auto_state, g_auto_weight, 0);
}

// ---------------------------------------------------------------------------
//  Level 1: building
// ---------------------------------------------------------------------------

static void wpPrimaryClicked(lv_event_t *e) {
  const float g = newG();
  if (sm_archived) {
    logSDf("UI: weight popup -> reactivate spool %d at %.0f g", sm_id, g);
    // Deferred like every other network call reached from a callback.
    reactivate_weight_g = g;
    reactivate_pending  = true;
  } else {
    logSDf("UI: weight popup -> send %.0f g for spool %d (bag %s)", g, sm_id, s_bag_on ? "on" : "off");
    confirmQueueWeight(g, s_gross_g - bagG());
    // Sent by hand: the automatic weighing must not send this spool again,
    // and a button that said "check" says what happened instead. Only with
    // the automatic on, which is what puts the button back once the spool
    // comes off.
    if (g_auto_weight) {
      auto_weight_done = true;
      weightButtonShow(WL_SENT, g);
    }
  }
  closeWeightPopups();
}

// The parts on the spool (clip, tag holder ...), top right: what comes off
// the reading, a tap to change it. Only once any are set up in the web
// settings. It stands beside both header lines, 44 px high to be hit, so the
// first line and the difference make room for it rather than run under it.
// The chooser opens the popup again on its way out, through
// wpReopenAfterParts(): a fresh reading that takes the new choice off, the
// bag switch as it was. Returns the width the header lines give up, 0
// without the chip.
static void wpReopenAfterParts();
static int wpBuildPartsChip(lv_obj_t *box) {
  if (!spoolExtrasAny()) return 0;
  lv_obj_t *chip = lv_btn_create(box);
  lv_obj_set_size(chip, WP_CHIP_W, WP_CHIP_H);
  lv_obj_set_pos(chip, WP_X + WP_INNER_W - WP_CHIP_W, WP_CHIP_Y);
  lv_obj_set_style_bg_color(chip, lv_color_hex(UI_COL_CHIP), 0);
  lv_obj_set_style_bg_color(chip, lv_color_hex(UI_COL_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_radius(chip, UI_RADIUS_BTN, 0);
  lv_obj_set_style_shadow_width(chip, 0, 0);
  lv_obj_set_style_pad_all(chip, 0, 0);
  char buf[24];
  snprintf(buf, sizeof(buf), LV_SYMBOL_EDIT " -%.0f g", spoolExtrasNow());
  lv_obj_center(wpLabel(chip, buf, UI_FONT_SMALL, UI_COL_INK_2));
  lv_obj_add_event_cb(chip, [](lv_event_t *e) {
    logSD("UI: weight popup -> parts");
    s_parts_for = sm_id;
    closeWeightPopups();
    showExtrasChooser(wpReopenAfterParts);
  }, LV_EVENT_CLICKED, NULL);
  return WP_CHIP_W + WP_GAP;
}

static void wpBuildHeaderAndPrimary(lv_obj_t *box) {
  const int chip = wpBuildPartsChip(box);
  s_v.line1 = wpLabel(box, "", UI_FONT_SMALL, UI_COL_INK_SOFT);
  lv_label_set_long_mode(s_v.line1, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_v.line1, WP_INNER_W - chip);
  lv_obj_set_pos(s_v.line1, WP_X, 6);
  s_v.change = wpLabel(box, "", UI_FONT_BODY, UI_COL_INK_2);
  lv_obj_set_pos(s_v.change, WP_X, 28);
  s_v.diff = wpLabel(box, "", UI_FONT_BODY, UI_COL_INK_SOFT);
  lv_obj_align(s_v.diff, LV_ALIGN_TOP_RIGHT, -WP_X - chip, 28);

  s_v.primary = wpButton(box, WP_X, WP_PRIMARY_Y, WP_INNER_W, WP_PRIMARY_H);
  lv_obj_set_style_radius(s_v.primary, UI_RADIUS_BOX, 0);
  s_v.main_lbl = wpLabel(s_v.primary, "", UI_FONT_ICON, UI_COL_OK_TEXT);
  s_v.main_sub = wpLabel(s_v.primary, "", UI_FONT_CAPTION, UI_COL_OK_TEXT_2);
  lv_obj_add_event_cb(s_v.primary, wpPrimaryClicked, LV_EVENT_CLICKED, NULL);
}

static void wpBuildSwitches(lv_obj_t *box) {
  int y = WP_ROW_Y0;
  if (bagInUse()) {
    const WpSwitch s = wpSwitchRow(box, y, LV_SYMBOL_DRIVE, STR_WP_BAG_ROW);
    s_v.bag_row = s.row; s_v.bag_sub = s.sub; s_v.bag_state = s.state;
    lv_obj_add_event_cb(s.row, [](lv_event_t *e) {
      s_bag_on = !s_bag_on;
      logSDf("UI: weight popup -> bag %s", s_bag_on ? "on" : "off");
      wpRefresh();
    }, LV_EVENT_CLICKED, NULL);
    y += WP_ROW_STEP;
  }
  // A spool shown by its id is never weighed on its own, so the switch
  // would have nothing to switch.
  if (!idSpoolActive()) {
    const WpSwitch s = wpSwitchRow(box, y, LV_SYMBOL_REFRESH, STR_WP_AUTO_ROW);
    s_v.auto_row = s.row; s_v.auto_state = s.state;
    lv_label_set_text(s.sub, T(STR_WP_AUTO_SUB));
    lv_obj_add_event_cb(s.row, [](lv_event_t *e) {
      autoWeightSwitch(!g_auto_weight);
      wpRefresh();
    }, LV_EVENT_CLICKED, NULL);
  }
}

static void wpShowOptions();

static void wpBuildBottom(lv_obj_t *box, int y) {
  const bool archive = !sm_archived;
  int x = WP_X;
  if (archive) {
    // Quiet on purpose: rarely wanted, and the question behind it catches a
    // stray tap.
    lv_obj_t *b = wpButton(box, x, y, WP_ARCHIVE_W, WP_BOTTOM_H);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_SURFACE), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_ARCHIVE_BG_PRESSED), LV_STATE_PRESSED);
    // A 1 px outline in the faint tone read as no button at all on the panel.
    lv_obj_set_style_border_width(b, UI_LEAVE_BORDER_W, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(UI_COL_ARCHIVE_BG_PRESSED), 0);
    char buf[40];
    snprintf(buf, sizeof(buf), LV_SYMBOL_TRASH "  %s", T(STR_WP_ARCHIVE));
    lv_obj_center(wpLabel(b, buf, UI_FONT_SMALL, UI_COL_ARCHIVE_TEXT));
    lv_obj_add_event_cb(b, [](lv_event_t *e) {
      closeWeightPopups();
      showConfirmPopup(T(STR_ARCHIVE_CONFIRM), 3);
    }, LV_EVENT_CLICKED, NULL);
    x += WP_ARCHIVE_W + WP_GAP;
  }
  const int w = (WP_X + WP_INNER_W - x - WP_GAP) / 2;
  lv_obj_t *opt = wpButton(box, x, y, w, WP_BOTTOM_H);
  char buf[40];
  snprintf(buf, sizeof(buf), LV_SYMBOL_SETTINGS "  %s", T(STR_WP_OPTIONS));
  lv_obj_center(wpLabel(opt, buf, UI_FONT_SMALL, UI_COL_INK_2));
  lv_obj_add_event_cb(opt, [](lv_event_t *e) { wpShowOptions(); }, LV_EVENT_CLICKED, NULL);

  lv_obj_t *close = wpButton(box, x + w + WP_GAP, y, w, WP_BOTTOM_H);
  lv_obj_center(wpLabel(close, T(STR_BT_CLOSE), UI_FONT_SMALL, UI_COL_INK_2));
  styleLeaveButton(close);
  lv_obj_add_event_cb(close, [](lv_event_t *e) { closeWeightPopups(); }, LV_EVENT_CLICKED, NULL);
}

// fresh: a new look at the pad. Coming back from Options keeps the reading
// and the bag switch the user had set.
static void wpOpenLevel1(bool fresh) {
  if (fresh) {
    s_gross_g = spoolExtrasBareReading();
    s_bag_on = false;
  }
  closeWeightPopups();
  // Room for both switch rows even when one is missing: the popup keeps its
  // height and the bottom row its place, and less of the screen behind it
  // shows through.
  const int bottom_y = WP_ROW_Y0 + WP_SWITCH_ROWS_MAX * WP_ROW_STEP;
  lv_obj_t *box = uiPopupBox(&s_popup, WP_BOX_W, bottom_y + WP_BOTTOM_H + WP_BOX_EXTRA);
  wpBuildHeaderAndPrimary(box);
  wpBuildSwitches(box);
  wpBuildBottom(box, bottom_y);
  wpRefresh();
}

void showWeightPopup() {
  logSDf("SHOW: WeightPopup (spool %d, %.0f g on the pad)", sm_id, scale_weight_g);
  wpOpenLevel1(true);
}

// Fork. Wurzel: the chooser came back through showWeightPopup(), a fresh
// open that turns the bag switch off: bag on, chip, back, and the large
// button carried the bag weight again with nothing on screen to say why.
// Fix: this way back reads the pad again, so the new parts come off, and
// keeps the switch; another spool on the screen by now gets a fresh open.
// NOT-TO-DO: keeping the switch on every open: the original turns it off for
// each new look at a spool on purpose (see s_bag_on).
// See test_weight_popup.py.
static void wpReopenAfterParts() {
  if (sm_id != s_parts_for) { showWeightPopup(); return; }
  logSDf("SHOW: WeightPopup after the parts (spool %d, bag %s)", sm_id, s_bag_on ? "on" : "off");
  s_gross_g = spoolExtrasBareReading();
  wpOpenLevel1(false);
}

// ---------------------------------------------------------------------------
//  Level 2: Options
// ---------------------------------------------------------------------------

static lv_obj_t* wpOptionRow(lv_obj_t *box, int y, const char *icon, int title_id, const char *sub) {
  lv_obj_t *b = wpButton(box, WP_X, y, WP_INNER_W, WP_OPT_ROW_H);
  lv_obj_set_style_radius(b, UI_RADIUS_BOX, 0);
  lv_obj_align(wpLabel(b, icon, UI_FONT_HEADLINE, UI_COL_ACCENT), LV_ALIGN_LEFT_MID, 14, 0);
  lv_obj_align(wpLabel(b, T(title_id), UI_FONT_BODY, UI_COL_INK), LV_ALIGN_LEFT_MID, 50, -10);
  lv_obj_align(wpLabel(b, sub, UI_FONT_CAPTION, UI_COL_CAPTION), LV_ALIGN_LEFT_MID, 50, 12);
  lv_obj_align(wpLabel(b, LV_SYMBOL_RIGHT, UI_FONT_BODY, UI_COL_INK_FAINT), LV_ALIGN_RIGHT_MID, -14, 0);
  return b;
}

// The rows the empty spool weight takes under Options. None where no scope
// can take an empty spool weight; elsewhere the original's two, weigh and
// type in. Behind Bambuddy in Spoolman mode the fork offers four ways (weigh,
// type in, spool type, full spool), and they sit behind one row that opens
// its choice of four (showTareChoice()): the box is 226 px high with it.
// NOT-TO-DO: no row of their own for each way: Full and four rows would need
// a box of 40 + 5 x 64 + 46 + 12 = 418 px, and even four rows 354 px, on a
// screen 320 px high.
static int wpTareRows(bool tare) {
  if (!tare) return 0;
  return spoolTypesActive() ? 1 : 2;
}

static void wpBuildOptionRows(lv_obj_t *box, bool tare) {
  char sub[64];
  snprintf(sub, sizeof(sub), T(STR_WP_FULL_SUB), confirmNewSpoolShownG(newG()));
  // An inherited empty weight carries into the filament weight measured
  // against it, so the row says where it came from, in the words of the
  // spool details (" (geerbt)" behind Bambuddy in Spoolman mode). Empty for
  // the spool's own and for none, so never where Full derives it.
  const size_t used = strlen(sub);
  snprintf(sub + used, sizeof(sub) - used, "%s", spoolTareNote(sm_tare_source));
  lv_obj_t *full = wpOptionRow(box, WP_OPT_ROW_Y0, LV_SYMBOL_PLUS, STR_WP_FULL, sub);
  lv_obj_add_event_cb(full, [](lv_event_t *e) {
    const float g = newG();
    logSDf("UI: weight options -> full spool, %.0f g", g);
    closeWeightPopups();
    confirmNewSpool(g);
  }, LV_EVENT_CLICKED, NULL);
  // Not offered where no scope can take an empty spool weight. In the fork
  // every backend takes the spool's own (backendCanTareSpool(), BamBuddy
  // behind Spoolman too since #2908), so this holds for none at present.
  if (!tare) return;

  if (wpTareRows(tare) == 1) {
    lv_obj_t *empty = wpOptionRow(box, WP_OPT_ROW_Y0 + WP_OPT_ROW_STEP, LV_SYMBOL_DOWNLOAD,
                                  STR_WP_TARE_ROW, T(STR_WP_TARE_ROW_SUB));
    lv_obj_add_event_cb(empty, [](lv_event_t *e) {
      logSD("UI: weight options -> empty weight");
      closeWeightPopups();
      showTareChoice();
    }, LV_EVENT_CLICKED, NULL);
    return;
  }

  // The empty spool without the parts on it: they are not the spool's.
  snprintf(sub, sizeof(sub), T(STR_WP_WEIGH_EMPTY_SUB), spoolExtrasBareReading());
  lv_obj_t *weigh = wpOptionRow(box, WP_OPT_ROW_Y0 + WP_OPT_ROW_STEP, LV_SYMBOL_DOWNLOAD,
                                STR_WP_WEIGH_EMPTY, sub);
  lv_obj_add_event_cb(weigh, [](lv_event_t *e) {
    // The reading at the moment of the press.
    const float g = spoolExtrasBareReading();
    closeWeightPopups();
    showSpoolWeightScope(g, true);
  }, LV_EVENT_CLICKED, NULL);

  lv_obj_t *type = wpOptionRow(box, WP_OPT_ROW_Y0 + 2 * WP_OPT_ROW_STEP, LV_SYMBOL_KEYBOARD,
                               STR_WP_TYPE_EMPTY, T(STR_WP_TYPE_EMPTY_SUB));
  lv_obj_add_event_cb(type, [](lv_event_t *e) {
    closeWeightPopups();
    showTareEntryPad();
  }, LV_EVENT_CLICKED, NULL);
}

static void wpShowOptions() {
  logSD("SHOW: WeightOptions");
  const bool tare = backendCanTareSpool() || backendCanTareFilamentOrVendor();
  const int back_y = WP_OPT_ROW_Y0 + (1 + wpTareRows(tare)) * WP_OPT_ROW_STEP;
  releaseScreen(&s_popup);
  lv_obj_t *box = uiPopupBox(&s_opts, WP_BOX_W, back_y + WP_BOTTOM_H + WP_BOX_EXTRA);

  lv_obj_set_pos(wpLabel(box, T(STR_WP_OPTIONS), UI_FONT_TITLE, UI_COL_INK), WP_X + 4, 10);
  char buf[48];
  snprintf(buf, sizeof(buf), "#%d  %s", sm_id, sm_filament_name);
  lv_obj_t *who = wpLabel(box, buf, UI_FONT_SMALL, UI_COL_INK_SOFT);
  lv_obj_align(who, LV_ALIGN_TOP_RIGHT, -WP_X - 4, 12);

  wpBuildOptionRows(box, tare);

  lv_obj_t *back = wpButton(box, WP_X, back_y, WP_INNER_W, WP_BOTTOM_H);
  lv_obj_center(wpLabel(back, T(STR_BACK), UI_FONT_SMALL, UI_COL_INK_2));
  styleLeaveButton(back, LV_SYMBOL_LEFT);
  lv_obj_add_event_cb(back, [](lv_event_t *e) { wpOpenLevel1(false); }, LV_EVENT_CLICKED, NULL);
}
