#include "bluetooth_screen.h"
#include "navigation.h"
#include "app/app_state.h"
#include "app/deferred_actions.h"

#include <Arduino.h>
#include <lvgl.h>

#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/ble_service.h"
#include "services/label_printer.h"
#include "ui/ble_devices_screen.h"
#include "ui/reboot_popup.h"
#include "ui/header_status.h"
#include "ui/info_screen.h"
#include "ui/print_card.h"
#include "ui/printer_screen.h"
#include "ui/theme.h"
#include "reboot_popup.h"
#include "ui_common.h"

// ============================================================
//  LABEL PRINTER
//
//  The settings screen behind the label printer tile: what the
//  scale prints its labels on, off, Bluetooth or a Brother on
//  the WLAN (design L1, ruling 1), and under the choice that
//  printer's rows. Bluetooth keeps the rows of the Bluetooth
//  screen this was: the device list, where everything a device
//  can do lives (ui/ble_devices_screen.cpp), and the printer.
//  The Brother's address, label and offset are set in the
//  browser, so its rows are the printer and a test print.
// ============================================================

// The test print result the WLAN rows were built with; a new one rebuilds.
static int s_shown_test = -1;

const char* labelPrinterLinkSymbol(LabelPrinterLink link) {
  if (link == LP_LINK_BLE)  return LV_SYMBOL_BLUETOOTH;
  if (link == LP_LINK_WLAN) return LV_SYMBOL_WIFI;
  return LV_SYMBOL_IMAGE;   // no link to show: the label printer itself
}

int labelPrinterLinkTitle(LabelPrinterLink link) {
  if (link == LP_LINK_BLE)  return STR_BT_TITLE;
  if (link == LP_LINK_WLAN) return STR_LP_WLAN;
  return STR_OFF;
}

static const char* linkLogName(LabelPrinterLink link) {
  return link == LP_LINK_WLAN ? "WLAN" : link == LP_LINK_BLE ? "Bluetooth" : "off";
}

// Title and subtitle of the row below, by what is set up: no printer, one
// of ours (then its labels, and the ribbon where it takes one), another one.
static void recommendTexts(StringID* title, StringID* sub) {
  const LabelPrinterConfig c = labelPrinterLoadConfig();
  if (!labelPrinterConfigured(c)) { *title = STR_BT_REC_NONE; *sub = STR_BT_REC_NONE_SUB; return; }
  if (!labelPrinterRecommended(c.model)) { *title = STR_BT_REC_OTHER; *sub = STR_BT_REC_OTHER_SUB; return; }
  *title = labelPrinterProfile(c.model).direct_thermal ? STR_BT_REC_LABELS : STR_BT_REC_RIBBON;
  *sub = STR_BT_REC_SUPPLY_SUB;
}

// The way to the manual's recommended printers and their labels, which the
// row words for the printer at hand. All three open the same page.
static void recommendRow(lv_obj_t* list) {
  StringID title, sub;
  recommendTexts(&title, &sub);
  char buf_t[40]; copyT(buf_t, sizeof(buf_t), title);
  char buf_s[64]; copyT(buf_s, sizeof(buf_s), sub);
  // The green subtitle of an active row, but the frame of a plain one
  // (Nikolai, 22.10.2026).
  lv_obj_t* btn = makeListBtn(list, LV_SYMBOL_LIST, buf_t, buf_s, true);
  lv_obj_set_style_border_color(btn, lv_color_hex(UI_COL_ROW_PRESSED), 0);
  lv_obj_add_event_cb(btn, [](lv_event_t* e) {
    logSD("BTN: Label printer -> recommended printers QR");
    showQRPopup(QR_PRINTERS);
  }, LV_EVENT_CLICKED, NULL);
}

// A tap on one of the three choices; the event's user data is the link.
static void linkPicked(lv_event_t* e) {
  const LabelPrinterLink link = (LabelPrinterLink)(intptr_t)lv_event_get_user_data(e);
  if (link == labelPrinterLink()) return;   // the chosen one changes nothing
  // Bluetooth as the switch did it: this boot gave the controller's memory
  // back because Bluetooth was off, and only a restart brings it back. The
  // choice is written by the restart button, so cancelling leaves it as it was.
  if (link == LP_LINK_BLE && !bleStackAvailable()) {
    logSD("BTN: Label printer -> Bluetooth, restart asked");
    showRebootPopup([]() { labelPrinterSetLink(LP_LINK_BLE); });
    return;
  }
  // Off and WLAN switch Bluetooth off at once, as the switch's off did, and
  // the devices the last scan saw go with it.
  const bool ble_was_on = bleEnabled();
  labelPrinterSetLink(link);
  if (ble_was_on && !bleEnabled()) bleDevicesForget();
  logSDf("BTN: Label printer -> %s", linkLogName(link));
  // Through the flag, not deleting the screen this button sits on from
  // inside its own callback.
  bluetooth_rebuild_pending = true;
}

// One choice, in the style of the switch row it replaces: the chosen one
// green and ticked on the right, the others without an arrow.
static void linkRow(lv_obj_t* list, LabelPrinterLink link, int sub_id) {
  const bool chosen = (labelPrinterLink() == link);
  char buf_t[40]; copyT(buf_t, sizeof(buf_t), labelPrinterLinkTitle(link));
  char buf_s[64]; copyT(buf_s, sizeof(buf_s), sub_id);
  lv_obj_t* btn = makeListBtn(list, labelPrinterLinkSymbol(link), buf_t, buf_s, chosen);
  markListBtnChosen(btn, chosen);
  lv_obj_add_event_cb(btn, linkPicked, LV_EVENT_CLICKED, (void*)(intptr_t)link);
}

// Bluetooth: the way to the devices, with what the last scan saw as the
// subtitle, and the printer, the one BLE device the scale has a use for.
static void bleRows(lv_obj_t* list) {
  char buf_t[40]; copyT(buf_t, sizeof(buf_t), STR_BT_DEVICES);
  char buf_sub[48];
  if (!bleDevicesScanned()) copyT(buf_sub, sizeof(buf_sub), STR_BT_DEVICES_NONE_YET);
  else snprintf(buf_sub, sizeof(buf_sub), T(STR_BT_DEVICES_FMT), bleDevicesCount());
  lv_obj_t *btn = makeListBtn(list, LV_SYMBOL_LIST, buf_t, buf_sub);
  lv_obj_add_event_cb(btn, [](lv_event_t *e){
    logSD("BTN: Label printer -> Devices");
    show_ble_devices_pending = true;
  }, LV_EVENT_CLICKED, NULL);

  const LabelPrinterConfig c = labelPrinterLoadConfig();
  char buf_p[40]; copyT(buf_p, sizeof(buf_p), STR_PRN_TITLE);
  char buf_ps[64];
  if (!labelPrinterConfigured(c)) copyT(buf_ps, sizeof(buf_ps), STR_PRN_NONE);
  else snprintf(buf_ps, sizeof(buf_ps), "%s  %s", labelPrinterProfile(c.model).name,
                c.name[0] ? c.name : c.address);
  lv_obj_t *pbtn = makeListBtn(list, LV_SYMBOL_IMAGE, buf_p, buf_ps, labelPrinterConfigured(c));
  lv_obj_add_event_cb(pbtn, [](lv_event_t *e){
    logSD("BTN: Label printer -> Printer");
    show_printer_pending = true;
  }, LV_EVENT_CLICKED, NULL);
}

// WLAN: the Brother with its address, the way to its printer screen, and
// the test print right here, with how the last one went.
static void wlanRows(lv_obj_t* list) {
  const LabelPrinterConfig c = labelPrinterLoadConfig();
  const LabelPrinterProfile& p = labelPrinterProfile(c.model);
  const bool have = labelPrinterConfigured(c);
  { char buf_t[40]; copyT(buf_t, sizeof(buf_t), STR_PRN_TITLE);
    char buf_s[64];
    if (!have) copyT(buf_s, sizeof(buf_s), STR_PRN_HOST_NONE);
    else snprintf(buf_s, sizeof(buf_s), "%s %s  %s", p.brand, p.name, c.address);
    lv_obj_t *btn = makeListBtn(list, LV_SYMBOL_IMAGE, buf_t, buf_s, have);
    lv_obj_add_event_cb(btn, [](lv_event_t *e){
      logSD("BTN: Label printer -> Printer");
      show_printer_pending = true;
    }, LV_EVENT_CLICKED, NULL); }

  // The verdict of the last test print in place of what the row does. A
  // Brother's "sent" is said by its card title; its message is the hint for
  // a label that does not come out, which the card showed in full.
  { char buf_t[40]; copyT(buf_t, sizeof(buf_t), STR_PRN_TEST);
    const int last = printerLastTestResult();
    int sub = STR_PRN_TEST_SUB;
    if (last == STR_PRN_NET_SENT_MSG) sub = STR_PRN_NET_SENT_TITLE;
    else if (last >= 0) sub = last;
    lv_obj_t *btn = makeListBtn(list, LV_SYMBOL_OK, buf_t, T(sub));
    if (last >= 0) {
      // One line, cut with dots: a verdict of the other printer's, kept
      // since boot, can be two lines long. Child 2 is makeListBtn()'s
      // subtitle; it needs a height of its own before dots engage.
      lv_obj_t *sub_lbl = lv_obj_get_child(btn, 2);
      if (sub_lbl) {
        lv_obj_set_height(sub_lbl, lv_font_get_line_height(UI_FONT_CAPTION));
        lv_label_set_long_mode(sub_lbl, LV_LABEL_LONG_DOT);
      }
    }
    lv_obj_add_event_cb(btn, [](lv_event_t *e){
      logSD("BTN: Label printer -> test print");
      // Renders and prints from the loop: the print blocks for seconds.
      printer_test_pending = true;
    }, LV_EVENT_CLICKED, NULL); }
}

void closeBluetoothScreen() {
  if (scr_bluetooth) { lv_obj_del(scr_bluetooth); scr_bluetooth = nullptr; }
}

void buildBluetoothScreen() {
  logSD("BUILD: LabelPrinterScreen");
  releaseScreen(&scr_bluetooth);
  scr_bluetooth = buildOverlayScreen();
  buildSubHeader(scr_bluetooth, T(STR_LP_TITLE),
    [](lv_event_t *e){
      logSD("BTN: Back -> Connection");
      // Deferred: the Connection screen is rebuilt so its tile shows the
      // choice, and not from inside the callback of the screen it replaces.
      show_connection_from_spoolman_pending = true;
    });
  addHeaderHelp(scr_bluetooth, STR_LP_TITLE, STR_LP_HELP);

  lv_obj_t *list = buildOptionList(scr_bluetooth);
  linkRow(list, LP_LINK_OFF, STR_LP_OFF_SUB);
  linkRow(list, LP_LINK_BLE, STR_LP_BLE_SUB);
  linkRow(list, LP_LINK_WLAN, STR_LP_WLAN_SUB);

  const LabelPrinterLink link = labelPrinterLink();
  s_shown_test = printerLastTestResult();
  if (link == LP_LINK_WLAN) {
    wlanRows(list);
    return;
  }
  if (link == LP_LINK_BLE) bleRows(list);
  // Also with the label printer off: it answers what the Bluetooth printers
  // are and which ones to get.
  recommendRow(list);
}

void handleBluetoothDeferredActions() {
  // A test print from the WLAN rows: once its card is closed, the row says
  // how it went. Only while the screen shows; every way back builds it anew.
  if (scr_bluetooth && s_shown_test != printerLastTestResult() && !printCardOpen() &&
      !lv_obj_has_flag(scr_bluetooth, LV_OBJ_FLAG_HIDDEN))
    bluetooth_rebuild_pending = true;
  if (bluetooth_rebuild_pending) {
    bluetooth_rebuild_pending = false;
    if (scr_bluetooth) {
      buildBluetoothScreen();        // releases the previous instance itself
      lv_obj_clear_flag(scr_bluetooth, LV_OBJ_FLAG_HIDDEN);
    }
    // The header chip follows Bluetooth; the main screen is alive underneath.
    updateHeaderStatus();
  }
}
