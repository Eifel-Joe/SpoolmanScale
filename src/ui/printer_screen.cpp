#include "printer_screen.h"
#include "navigation.h"
#include "app/app_state.h"
#include "app/deferred_actions.h"
#include "app/label_server.h"
#include "app/label_spool.h"

#include <Arduino.h>
#include <lvgl.h>

#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/ble_service.h"
#include "services/brother_ql.h"
#include "services/label_layout.h"
#include "services/label_printer.h"
#include "services/label_render.h"
#include "ui/choice_grid.h"
#include "ui/info_popup.h"
#include "ui/print_card.h"
#include "ui/print_sheet.h"
#include "ui/printer_offset_screen.h"
#include "ui/theme.h"
#include "ui_common.h"


// A verdict belongs to the printer it came from: kept with the link it was
// printed on, and none once another one is chosen, so after a switch from
// Bluetooth to WLAN the Brother's row and the browser show no error of the
// Bluetooth printer's.
struct KeptResult { int text; LabelPrinterLink link; };
static KeptResult s_last_test = { -1, LP_LINK_OFF };
static KeptResult s_last_label = { -1, LP_LINK_OFF };

static int keptForLink(const KeptResult& r) {
  return r.link == labelPrinterLink() ? r.text : -1;
}

// The verdict a Brother's "sent" was kept in, the test's or the label's: what
// the printer says in the 10 s after replaces it (brotherReplyTick()), so the
// browser's line and the test row do not keep saying "sent" when the card
// said otherwise. Null when the last attempt was not a Brother's "sent".
static KeptResult* s_brother_kept = nullptr;

// The start of every print attempt: the line the Brother's label before still
// holds ends here (brother_ql.h), so a late reply cannot take the place of
// this attempt's answer, nor overwrite its kept verdict.
static void printAttemptStart() {
  brotherQlRelease();
  s_brother_kept = nullptr;
}

static void keepVerdict(KeptResult& kept, LabelPrintResult result, LabelPrinterLink link) {
  kept = { labelPrintResultString(result), link };
  if (result == LP_NET_SENT) s_brother_kept = &kept;
}

// A label that needs no spool, the test label or the calibration page, under
// the print card; the verdict is kept for the browser's last-print line.
static void printFixedLabel(bool (*render)(const LabelPrinterConfig&, LabelRaster*),
                            const char* what) {
  printerOffsetFlush();
  const LabelPrinterLink link = labelPrinterLink();
  const LabelPrinterConfig c = labelPrinterLoadConfig();
  printAttemptStart();
  // The one question behind every print button (design L1, ruling 9).
  LabelPrintResult result = labelPrinterReadiness();
  if (result == LP_OK) {
    printCardShow();
    LabelRaster raster{};
    if (!render(c, &raster)) result = LP_BAD_RASTER;
    else result = labelPrinterPrint(c, raster, printCardTick);
    labelRasterFree(&raster);
  }
  logSDf("Printer: %s result=%d", what, (int)result);
  keepVerdict(s_last_test, result, link);
  printCardResult(result);
}
int printerLastTestResult() { return keptForLink(s_last_test); }
int printerLastLabelResult() { return keptForLink(s_last_label); }

// Where the back button leads: the print sheet, when that opened the screen.
static bool s_back_to_sheet = false;
void printerScreenFromSheet() { s_back_to_sheet = true; }

// The spool whose server label failed, for the card's "own layout" answer.
static SpoolLabelData s_own_layout_spool{};

// The raster of a spool's label: the FilaMan preset the editor chose, or the
// saved template. The scale's own needs the dates first; a slow backend only
// costs the label its date. The server draws its label with what it holds.
// The filament's label is always the scale's own; it wants the temperatures.
static LabelPrintResult spoolRaster(const LabelPrinterConfig& c, SpoolLabelData& spool,
                                    bool server, LabelKind kind, LabelRaster* raster) {
  if (server)
    return labelServerPrintRaster(c, spool.id, raster, printCardTick) == 200 ? LP_OK
                                                                             : LP_SERVER_LABEL;
  labelSpoolFetchDates(&spool);
  const bool ok = kind == LABEL_KIND_FILAMENT
      ? labelRenderFilament(c, labelLayoutLoad(LABEL_KIND_FILAMENT), spool, raster)
      : labelRenderSpool(c, labelLayoutLoad(), spool, raster);
  return ok ? LP_OK : LP_BAD_RASTER;
}

// A spool's label under the print card. The card comes first, so the wait
// for the dates or the server has something on screen. own_layout: the
// scale's template even where a FilaMan preset is chosen.
static void printSpoolLabel(SpoolLabelData spool, bool own_layout = false,
                            LabelKind kind = LABEL_KIND_SPOOL) {
  const LabelPrinterLink link = labelPrinterLink();
  const LabelPrinterConfig c = labelPrinterLoadConfig();
  const bool server = !own_layout && kind == LABEL_KIND_SPOOL && labelServerInUse();
  printAttemptStart();
  LabelPrintResult result = labelPrinterReadiness();
  if (result == LP_OK) {
    printCardShow();
    LabelRaster raster{};
    result = spoolRaster(c, spool, server, kind, &raster);
    if (result == LP_OK) result = labelPrinterPrint(c, raster, printCardTick);
    labelRasterFree(&raster);
  }
  logSDf("Printer: spool #%d %s label (%s) result=%d", spool.id,
         kind == LABEL_KIND_FILAMENT ? "filament" : "spool",
         server ? "FilaMan preset" : "own layout", (int)result);
  keepVerdict(s_last_label, result, link);
  // Never switched over unasked: the card offers the scale's own layout.
  if (result == LP_SERVER_LABEL) {
    s_own_layout_spool = spool;
    printCardResult(result, labelServerLastReason(), true);
  } else {
    printCardResult(result);
  }
}

void printerPrintPadLabel(LabelKind kind) {
  printerOffsetFlush();
  SpoolLabelData spool{};
  if (!labelSpoolFromScan(&spool)) {
    showInfoPopup(STR_PRN_TITLE, STR_PRN_ERR_NO_SPOOL, INFO_WARN);
    return;
  }
  printSpoolLabel(spool, false, kind);
}

// The label of the last spool scanned, on the pad or not: the print sheet
// and the browser's editor print what they showed.
void printerPrintLastLabel(LabelKind kind) {
  printerOffsetFlush();
  SpoolLabelData spool{};
  if (!labelSpoolLast(&spool)) {
    showInfoPopup(STR_PRN_TITLE, STR_PRN_ERR_NO_SPOOL, INFO_WARN);
    return;
  }
  printSpoolLabel(spool, false, kind);
}

void closePrinterScreen() {
  if (scr_printer) { lv_obj_del(scr_printer); scr_printer = nullptr; }
  closeChoiceGrid();
  // Closed by the X or by "Devices" as well as by its back button: the next
  // opening, from the label printer screen, must lead back there.
  s_back_to_sheet = false;
}

static void rebuild();

// Saves a picked model or stock and redraws the rows that name it.
static void saveAndRebuild(const LabelPrinterConfig& c) {
  if (!labelPrinterSaveConfig(c)) showInfoPopup(STR_PRN_TITLE, STR_ERR_SAVE, INFO_WARN);
  rebuild();
}

static void modelPicked(int index) {
  int count = 0;
  const LabelPrinterModel* models = labelPrinterModels(labelPrinterLink(), &count);
  if (index < 0 || index >= count) return;
  LabelPrinterConfig c = labelPrinterLoadConfig();
  if (c.model == models[index]) return;
  c.model = models[index];
  logSDf("Printer: model -> %s", labelPrinterProfile(c.model).name);
  // A stock the new model cannot take falls back to its default in the save.
  saveAndRebuild(c);
}

// Every model of the chosen link in the table's order: recommended ones say
// so, experimental ones warn. Under WLAN that is the one Brother.
static void showModelPicker() {
  const LabelPrinterModel now = labelPrinterLoadConfig().model;
  int count = 0;
  const LabelPrinterModel* models = labelPrinterModels(labelPrinterLink(), &count);
  ChoiceCard cards[CHOICE_GRID_MAX] = {};
  int current = -1;
  const int n = count < CHOICE_GRID_MAX ? count : CHOICE_GRID_MAX;
  for (int i = 0; i < n; i++) {
    const LabelPrinterProfile& p = labelPrinterProfile(models[i]);
    ChoiceCard& card = cards[i];
    card.caption = p.brand;
    snprintf(card.value, sizeof(card.value), "%s", p.name);
    card.enabled = true;
    if (labelPrinterRecommended(p.model)) {
      card.tag = CHOICE_TAG_GOOD; card.tag_text = STR_PRN_TAG_RECOMMENDED;
    } else if (p.experimental) {
      card.tag = CHOICE_TAG_WARN; card.tag_text = STR_PRN_TAG_EXPERIMENTAL;
    }
    if (p.model == now) current = i;
  }
  // A lone card takes the width, as on the stock grid.
  showChoiceGrid(STR_PRN_MODEL, cards, n, current, n < 3 ? n : 3, modelPicked);
}

// The sizes offered in the grid, in its order; filled when it opens.
static LabelMediaSize s_media_offer[CHOICE_GRID_MAX];

static void mediaPicked(int index) {
  if (index < 0 || index >= CHOICE_GRID_MAX) return;
  LabelPrinterConfig c = labelPrinterLoadConfig();
  c.media_width_mm = s_media_offer[index].width_mm;
  c.media_length_mm = s_media_offer[index].length_mm;
  logSDf("Printer: label stock -> %ux%u mm", (unsigned)c.media_width_mm, (unsigned)c.media_length_mm);
  saveAndRebuild(c);
}

static void addMediaCard(ChoiceCard* cards, int* n, const LabelMediaSize& s, bool enabled) {
  if (*n >= CHOICE_GRID_MAX) return;
  ChoiceCard& card = cards[*n];
  card.caption = nullptr;   // the outline says enough
  snprintf(card.value, sizeof(card.value), T(STR_PRN_MEDIA_FMT),
           (unsigned)s.width_mm, (unsigned)s.length_mm);
  card.shape_w_mm = s.width_mm;
  card.shape_h_mm = s.length_mm;
  card.enabled = enabled;
  s_media_offer[*n] = s;
  (*n)++;
}

// The sizes the model takes, each drawn to scale, then the planned ones it
// would take, greyed: a Brother takes none of those, its sizes are its own.
static void showMediaPicker() {
  const LabelPrinterConfig c = labelPrinterLoadConfig();
  int count = 0;
  const LabelMediaSize* sizes = labelPrinterMediaSizes(c.model, &count);
  ChoiceCard cards[CHOICE_GRID_MAX] = {};
  int n = 0, current = -1;
  for (int i = 0; i < count; i++) {
    const LabelMediaSize& s = sizes[i];
    if (!labelPrinterTakesMedia(c.model, s.width_mm, s.length_mm)) continue;
    if (s.width_mm == c.media_width_mm && s.length_mm == c.media_length_mm) current = n;
    addMediaCard(cards, &n, s, true);
  }
  for (int i = 0; i < LABEL_MEDIA_PLANNED_COUNT && n < CHOICE_GRID_MAX; i++) {
    const LabelMediaSize& s = LABEL_MEDIA_SIZES_PLANNED[i];
    if (!labelPrinterTakesMedia(c.model, s.width_mm, s.length_mm)) continue;
    cards[n].tag = CHOICE_TAG_WARN;
    cards[n].tag_text = STR_PRN_MEDIA_NO_LAYOUT;
    addMediaCard(cards, &n, s, false);
  }
  // Two or three cards share the width rather than leaving a gap.
  showChoiceGrid(STR_PRN_MEDIA, cards, n, current, n < 3 ? n : 3, mediaPicked);
}

void buildPrinterScreen() {
  logSD("BUILD: PrinterScreen");
  releaseScreen(&scr_printer);
  scr_printer = buildOverlayScreen();
  buildSubHeader(scr_printer, T(STR_PRN_TITLE),
    [](lv_event_t *e){
      if (s_back_to_sheet) {
        logSD("BTN: Back -> Print sheet");
        s_back_to_sheet = false;
        show_print_sheet_pending = true;
        return;
      }
      logSD("BTN: Back -> Label printer");
      // Deferred and a rebuild: the label printer screen's row names the
      // printer.
      show_bluetooth_pending = true;
    });
  addHeaderHelp(scr_printer, STR_PRN_TITLE, STR_PRN_HELP);

  const LabelPrinterConfig c = labelPrinterLoadConfig();
  const LabelPrinterProfile& p = labelPrinterProfile(c.model);
  const bool have = labelPrinterConfigured(c);
  const bool wlan = (labelPrinterLink() == LP_LINK_WLAN);
  lv_obj_t *list = buildOptionList(scr_printer);

  // The device: what was picked on the device card, and the way there. The
  // Brother has its address instead, set in the browser like its label and
  // offset (design L1, ruling 2); tapped, the row says so.
  if (wlan) { char buf_t[40]; copyT(buf_t, sizeof(buf_t), STR_PRN_DEVICE);
    char buf_s[64];
    if (!have) copyT(buf_s, sizeof(buf_s), STR_PRN_HOST_NONE);
    else snprintf(buf_s, sizeof(buf_s), "%s", c.address);
    lv_obj_t *btn = makeListBtn(list, LV_SYMBOL_WIFI, buf_t, buf_s, have);
    lv_obj_add_event_cb(btn, infoPopupEventCb, LV_EVENT_CLICKED,
                        INFO_POPUP_ARG(STR_PRN_DEVICE, STR_PRN_HOST_HELP));
  } else { char buf_t[40]; copyT(buf_t, sizeof(buf_t), STR_PRN_DEVICE);
    char buf_s[64];
    if (!have) copyT(buf_s, sizeof(buf_s), STR_PRN_DEVICE_NONE);
    else snprintf(buf_s, sizeof(buf_s), "%s  %s", c.name[0] ? c.name : T(STR_BT_UNNAMED), c.address);
    lv_obj_t *btn = makeListBtn(list, LV_SYMBOL_BLUETOOTH, buf_t, buf_s, have);
    lv_obj_add_event_cb(btn, [](lv_event_t *e){
      logSD("BTN: Printer -> Devices");
      show_ble_devices_pending = true;
    }, LV_EVENT_CLICKED, NULL); }

  // The model, picked from a grid.
  { char buf_t[40]; copyT(buf_t, sizeof(buf_t), STR_PRN_MODEL);
    char buf_s[64];
    if (p.experimental) snprintf(buf_s, sizeof(buf_s), "%s %s  %s", p.brand, p.name, T(STR_PRN_EXPERIMENTAL));
    else snprintf(buf_s, sizeof(buf_s), "%s %s", p.brand, p.name);
    lv_obj_t *btn = makeListBtn(list, LV_SYMBOL_SETTINGS, buf_t, buf_s);
    lv_obj_add_event_cb(btn, [](lv_event_t *e){
      logSD("BTN: Printer -> model grid");
      showModelPicker();
    }, LV_EVENT_CLICKED, NULL); }

  // The label stock, picked from a grid of what the model takes. On
  // thermal paper its "?" says what a dryer does to it.
  { char buf_t[40]; copyT(buf_t, sizeof(buf_t), STR_PRN_MEDIA);
    char buf_s[48];
    snprintf(buf_s, sizeof(buf_s), T(STR_PRN_MEDIA_FMT),
             (unsigned)c.media_width_mm, (unsigned)c.media_length_mm);
    lv_obj_t *help = nullptr;
    lv_obj_t *btn = makeListBtn(list, LV_SYMBOL_IMAGE, buf_t, buf_s, false,
                                p.direct_thermal ? &help : nullptr);
    if (help) lv_obj_add_event_cb(help, infoPopupEventCb, LV_EVENT_CLICKED,
                                  INFO_POPUP_ARG(STR_PRN_MEDIA, STR_PRN_HEAT_HELP));
    lv_obj_add_event_cb(btn, [](lv_event_t *e){
      logSD("BTN: Printer -> label stock grid");
      showMediaPicker();
    }, LV_EVENT_CLICKED, NULL); }

  // Whether "Print label" shows the print sheet first or prints at once.
  { char buf_t[40]; copyT(buf_t, sizeof(buf_t), STR_PS_ASK);
    char buf_sub[48]; copyT(buf_sub, sizeof(buf_sub), STR_PS_ASK_SUB);
    const bool ask = labelPrinterAskFirst();
    lv_obj_t *btn = makeListBtn(list, LV_SYMBOL_EYE_OPEN, buf_t, buf_sub, ask);
    lv_obj_t *state = lv_obj_get_child(btn, -1);
    if (state) {
      lv_label_set_text(state, T(ask ? STR_ON : STR_OFF));
      lv_obj_set_style_text_color(state, lv_color_hex(ask ? UI_COL_ACCENT : UI_COL_CAPTION), 0);
      lv_obj_set_style_text_font(state, UI_FONT_SMALL, 0);
    }
    lv_obj_add_event_cb(btn, [](lv_event_t *e){
      logSD("BTN: Printer -> print sheet on/off");
      printer_toggle_ask_pending = true;
    }, LV_EVENT_CLICKED, NULL); }

  // The test print: the first thing to try with a new printer.
  { char buf_t[40]; copyT(buf_t, sizeof(buf_t), STR_PRN_TEST);
    char buf_s[64]; copyT(buf_s, sizeof(buf_s), STR_PRN_TEST_SUB);
    lv_obj_t *btn = makeListBtn(list, LV_SYMBOL_OK, buf_t, buf_s);
    lv_obj_add_event_cb(btn, [](lv_event_t *e){
      logSD("BTN: Printer -> test print");
      // Renders and prints from the loop: the print blocks for seconds.
      printer_test_pending = true;
    }, LV_EVENT_CLICKED, NULL); }

  // Where the roll runs under the head: its own screen, with the
  // calibration page. The row says the offset and, at an edge or the
  // middle, which one. Only with a printer: the offset is kept per device
  // (its address is the key), and without one a value set here was reported
  // saved and was 0 again after a restart.
  if (have) { char buf_t[40]; copyT(buf_t, sizeof(buf_t), STR_W_P_CAL_TITLE);
    int16_t lo, hi;
    labelPrinterOffsetRange(c, &lo, &hi);
    const int off = labelPrinterOffset(c);
    const int per = labelPrinterDotsForMm(c.model, 1);
    const int mm = (off + (off < 0 ? -per / 2 : per / 2)) / per;
    int where = -1;
    if (off == 0) where = STR_W_P_CAL_CENTER;
    else if (off == hi && hi > 0) where = STR_W_P_CAL_RIGHT;
    else if (off == lo && lo < 0) where = STR_W_P_CAL_LEFT;
    char buf_s[64];
    if (where < 0) snprintf(buf_s, sizeof(buf_s), "%s%d mm", mm > 0 ? "+" : "", mm);
    else snprintf(buf_s, sizeof(buf_s), "%s%d mm  \xE2\x80\xA2  %s", mm > 0 ? "+" : "", mm, T(where));
    lv_obj_t *btn = makeListBtn(list, LV_SYMBOL_EDIT, buf_t, buf_s);
    lv_obj_add_event_cb(btn, [](lv_event_t *e){
      logSD("BTN: Printer -> print position");
      show_printer_offset_pending = true;
    }, LV_EVENT_CLICKED, NULL); }

  // Not for the Brother: its address is emptied in the browser.
  if (have && !wlan) {
    char buf_t[40]; copyT(buf_t, sizeof(buf_t), STR_PRN_FORGET);
    lv_obj_t *btn = makeListBtn(list, LV_SYMBOL_TRASH, buf_t, "");
    // In the red of every row that deletes something, like the factory reset
    // in the system screen: set apart from the green rows, not shouting.
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COL_DANGER_ROW), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COL_BAD_BG), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, lv_color_hex(UI_COL_BAD_BG_PRESSED), 0);
    for (uint32_t i = 0; i < 2; i++) {   // child 0 the icon, 1 the title
      lv_obj_t *l = lv_obj_get_child(btn, i);
      if (l) lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_DANGER_TEXT), 0);
    }
    lv_obj_add_event_cb(btn, [](lv_event_t *e){
      logSD("BTN: Printer -> forget");
      printer_forget_pending = true;
    }, LV_EVENT_CLICKED, NULL);
  }
}

static void rebuild() {
  if (!scr_printer) return;
  buildPrinterScreen();            // releases the previous instance itself
  lv_obj_clear_flag(scr_printer, LV_OBJ_FLAG_HIDDEN);
}

// What a Brother said in the 10 s its line is held after a label
// (brother_ql.h): its bytes, or that it broke the line, on the print card
// again, which waits for OK (Joerg, 26.10.2026). The printer closing the line
// without a word is its normal end and only in the log. Looked at once per
// pass; between labels there is no hold and nothing to do.
// The bytes the card shows: two lines of hex under its one line of text (a
// byte takes some 24 px of the 360); the log has every byte of the chunk.
#define BROTHER_REPLY_SHOWN 24

static void brotherReplyTick() {
  brotherQlTick();
  BrotherQlReply reply;
  if (!brotherQlTakeReply(&reply)) return;
  const bool bytes = (reply.kind == BQR_BYTES);
  // The kept verdict of that label says it too: its title for the bytes,
  // which only the card shows, the sentence for a broken line.
  if (s_brother_kept)
    s_brother_kept->text = bytes ? STR_PRN_NET_REPLY_TITLE : STR_PRN_NET_BROKEN_MSG;
  if (bytes) {
    char hex[BROTHER_REPLY_SHOWN * 3];
    brotherQlBytesHex(reply.bytes, reply.n, hex, sizeof(hex));
    printCardResult(LP_NET_REPLY, hex);
  } else {
    printCardResult(LP_NET_BROKEN);
  }
}

void handlePrinterDeferredActions() {
  handleChoiceGridDeferredActions();
  brotherReplyTick();
  printCardLoop();
  printSheetTick();
  printerOffsetTick();
  if (show_printer_offset_pending) {
    show_printer_offset_pending = false;
    buildPrinterOffsetScreen();
    hideAllOverlays();
    // Built hidden like every overlay; shown once the others are down.
    showPrinterOffsetScreen();
  }
  if (printer_toggle_ask_pending) {
    printer_toggle_ask_pending = false;
    if (!labelPrinterSetAskFirst(!labelPrinterAskFirst()))
      showInfoPopup(STR_PRN_TITLE, STR_ERR_SAVE, INFO_WARN);
    rebuild();
  }
  if (printer_forget_pending) {
    printer_forget_pending = false;
    labelPrinterForget();
    rebuild();
  }
  if (printer_test_pending) {
    printer_test_pending = false;
    printFixedLabel(labelRenderTest, "test print");
  }
  if (printer_calib_pending) {
    printer_calib_pending = false;
    printFixedLabel(labelRenderCalibration, "calibration page");
  }
  if (print_spool_label_pending) {
    print_spool_label_pending = false;
    printerPrintPadLabel(LABEL_KIND_SPOOL);
  }
  // The label editor's print: the last spool scanned, on the pad or not,
  // its own label or its filament's.
  if (print_last_label_pending || print_last_filament_label_pending) {
    const LabelKind kind = print_last_label_pending ? LABEL_KIND_SPOOL : LABEL_KIND_FILAMENT;
    print_last_label_pending = false;
    print_last_filament_label_pending = false;
    printerPrintLastLabel(kind);
  }
  if (print_own_layout_pending) {
    print_own_layout_pending = false;
    printSpoolLabel(s_own_layout_spool, true);
  }
  labelSpoolTick();
  labelServerTick();
}
