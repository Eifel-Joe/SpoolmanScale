#include "wifi_setup_screen.h"
#include "navigation.h"
#include "app/app_state.h"
#include "app/deferred_actions.h"

#include <Arduino.h>
#include <lvgl.h>
#include <cstring>

#include "header_status.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/backend.h"
#include "services/time_service.h"
#include "services/wifi_manager.h"
#include "setup_frame.h"
#include "ui_common.h"
#include "theme.h"

// The connect step of the WiFi setup: one card that says what is happening,
// then what came of it, and the bar with the way on. One state at a time -
// the old screen kept "Connecting to" standing over "Connected!".

#define CONN_CARD_X        12
#define CONN_CARD_Y        60
#define CONN_CARD_W       456
#define CONN_CARD_H       180
#define CONN_ICON          52
#define CONN_ICON_Y        30
#define CONN_TITLE_Y       96
#define CONN_DETAIL_Y     126
#define CONN_TEXT_W       420
#define CONN_SPIN_ARC       6

enum ConnResult : uint8_t { CONN_RUNNING, CONN_OK, CONN_FAILED };
static ConnResult conn_result = CONN_RUNNING;

// Held by name: the result writes into them, and the screen they sit on can
// be gone by then (closeWifiConnectingScreen() drops them).
static lv_obj_t *conn_spinner  = nullptr;
static lv_obj_t *conn_ok_disc  = nullptr;
static lv_obj_t *conn_bad_disc = nullptr;
static lv_obj_t *conn_title    = nullptr;
static lv_obj_t *conn_detail   = nullptr;
static SetupBar  conn_bar      = { nullptr, nullptr, nullptr };

void closeWifiConnectingScreen() {
  conn_spinner = conn_ok_disc = conn_bad_disc = conn_title = conn_detail = nullptr;
  conn_bar = { nullptr, nullptr, nullptr };
  releaseScreen(&scr_wifi_connecting);
}

static lv_obj_t *resultDisc(lv_obj_t *card, uint32_t bg, uint32_t ink, const char *symbol) {
  lv_obj_t *disc = lv_obj_create(card);
  lv_obj_remove_style_all(disc);
  lv_obj_set_size(disc, CONN_ICON, CONN_ICON);
  lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(disc, lv_color_hex(bg), 0);
  lv_obj_align(disc, LV_ALIGN_TOP_MID, 0, CONN_ICON_Y);
  lv_obj_add_flag(disc, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t *l = lv_label_create(disc);
  lv_label_set_text(l, symbol);
  lv_obj_set_style_text_font(l, UI_FONT_ICON, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(ink), 0);
  lv_obj_center(l);
  return disc;
}

static lv_obj_t *cardLine(lv_obj_t *card, const lv_font_t *font, uint32_t col, int y) {
  lv_obj_t *l = lv_label_create(card);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_size(l, CONN_TEXT_W, lv_font_get_line_height(font));
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
  lv_label_set_text(l, "");
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
  return l;
}

static void buildConnCard(lv_obj_t *scr) {
  lv_obj_t *card = lv_obj_create(scr);
  lv_obj_set_size(card, CONN_CARD_W, CONN_CARD_H);
  lv_obj_set_pos(card, CONN_CARD_X, CONN_CARD_Y);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_style_radius(card, UI_RADIUS_ROW, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(UI_COL_LINE_SOFT), 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  // It stands rather than turns: the connect holds the loop while it runs.
  conn_spinner = lv_spinner_create(card, 1000, 70);
  lv_obj_set_size(conn_spinner, CONN_ICON, CONN_ICON);
  lv_obj_align(conn_spinner, LV_ALIGN_TOP_MID, 0, CONN_ICON_Y);
  lv_obj_set_style_arc_width(conn_spinner, CONN_SPIN_ARC, LV_PART_MAIN);
  lv_obj_set_style_arc_width(conn_spinner, CONN_SPIN_ARC, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(conn_spinner, lv_color_hex(UI_COL_LINE), LV_PART_MAIN);
  lv_obj_set_style_arc_color(conn_spinner, lv_color_hex(UI_COL_ACCENT), LV_PART_INDICATOR);
  lv_obj_clear_flag(conn_spinner, LV_OBJ_FLAG_CLICKABLE);

  conn_ok_disc  = resultDisc(card, UI_COL_PICKED_BG, UI_COL_ACCENT, LV_SYMBOL_OK);
  conn_bad_disc = resultDisc(card, UI_COL_AMBER_BG, UI_COL_WARN, LV_SYMBOL_WARNING);
  conn_title  = cardLine(card, UI_FONT_BODY, UI_COL_INK, CONN_TITLE_Y);
  conn_detail = cardLine(card, UI_FONT_CAPTION, UI_COL_INK_SOFT, CONN_DETAIL_Y);
}

// Main button: on after a working link, to the password again after a
// failed one. Outside the setup "on" keeps leading straight to the address
// screen, as it always did.
static void onConnMain(lv_event_t *e) {
  if (conn_result == CONN_FAILED) {
    logSD("BTN: WifiConnecting -> Password again");
    showWifiPassScreen();
  } else if (setup_active) {
    logSD("BTN: WifiConnecting -> Next (Backend)");
    show_backend_pending = true;
  } else {
    logSD("BTN: WifiConnecting -> Next (Address)");
    show_spoolman_pending = true;
  }
}

static void showConnBar() {
  if (conn_bar.quiet) lv_obj_clear_flag(conn_bar.quiet, LV_OBJ_FLAG_HIDDEN);
  if (conn_bar.main)  lv_obj_clear_flag(conn_bar.main,  LV_OBJ_FLAG_HIDDEN);
}

static const char *signalWord(int rssi) {
  if (rssi >= -50) return T(STR_WIFI_QUAL_EXCELLENT);
  if (rssi >= -65) return T(STR_WIFI_QUAL_GOOD);
  if (rssi >= -75) return T(STR_WIFI_QUAL_MEDIUM);
  return T(STR_WIFI_QUAL_WEAK);
}

static void fillConnSuccess() {
  conn_result = CONN_OK;
  if (conn_spinner) lv_obj_add_flag(conn_spinner, LV_OBJ_FLAG_HIDDEN);
  if (conn_ok_disc) lv_obj_clear_flag(conn_ok_disc, LV_OBJ_FLAG_HIDDEN);
  char buf[96];
  snprintf(buf, sizeof(buf), T(STR_WIFI_CONNECTED_TO), cfg_wifi_ssid[0] ? cfg_wifi_ssid : "-");
  if (conn_title) lv_label_set_text(conn_title, buf);
  snprintf(buf, sizeof(buf), T(STR_WIFI_CONN_DETAIL),
           wifiManagerLocalIP().toString().c_str(), signalWord(wifiManagerRSSI()));
  if (conn_detail) lv_label_set_text(conn_detail, buf);

  if (conn_bar.main_label) {
    char nb[32];
    if (setup_active) copyT(nb, sizeof(nb), STR_BTN_NEXT);
    else              snprintf(nb, sizeof(nb), "%s", backendName());
    const size_t n = strlen(nb);
    snprintf(nb + n, sizeof(nb) - n, "  " LV_SYMBOL_RIGHT);
    lv_label_set_text(conn_bar.main_label, nb);
  }
  showConnBar();
}

static void fillConnFailure() {
  conn_result = CONN_FAILED;
  if (conn_spinner)  lv_obj_add_flag(conn_spinner, LV_OBJ_FLAG_HIDDEN);
  if (conn_bad_disc) lv_obj_clear_flag(conn_bad_disc, LV_OBJ_FLAG_HIDDEN);
  char buf[96];
  snprintf(buf, sizeof(buf), T(STR_WIFI_NO_LINK), cfg_wifi_ssid);
  if (conn_title)  lv_label_set_text(conn_title, buf);
  if (conn_detail) lv_label_set_text(conn_detail, T(STR_WIFI_FAIL_HINT));
  if (conn_bar.main_label) lv_label_set_text(conn_bar.main_label, T(STR_WIFI_PASS_AGAIN));
  showConnBar();
}

// The same screen for a link that is already up, typically one the web
// flasher set: no connect attempt, the result straight away.
void showWifiConnectedScreen() {
  logSD("SHOW: WifiConnectedScreen");
  logSD("UI: Screen -> WifiConnected");
  hideAllOverlays();
  closeWifiConnectingScreen();
  buildWifiConnectingScreen();
  fillConnSuccess();
  lv_obj_clear_flag(scr_wifi_connecting, LV_OBJ_FLAG_HIDDEN);
}

void showWifiConnectingScreen() {
  logSD("SHOW: WifiConnectingScreen");
  logSD("UI: Screen -> WifiConnecting");
  hideAllOverlays();
  closeWifiConnectingScreen();
  buildWifiConnectingScreen();
  lv_obj_clear_flag(scr_wifi_connecting, LV_OBJ_FLAG_HIDDEN);
  lv_timer_handler();

  wifi_ok = false;
  // Only without a link, as in doWifiScan(): a connected station (another
  // network picked after "change") is switched over directly. The reset would
  // turn the driver off under the web server, which is still listening:
  // wifi_ok was cleared just above, and no state sync has run since.
  if (!wifiManagerIsConnected()) wifiManagerPrepareScan();
  wifi_ok = wifiManagerConnect(cfg_wifi_ssid, cfg_wifi_password, 20, 500);
  lv_timer_handler();

  updateHeaderStatus();
  if (wifi_ok) {
    syncNTP();
    lv_label_set_text(lbl_spoolman_weight, T(STR_WAIT_SCAN_SM));
    fillConnSuccess();
  } else {
    fillConnFailure();
  }
  lv_timer_handler();
}

void buildWifiConnectingScreen() {
  logSD("BUILD: WifiConnectingScreen");
  closeWifiConnectingScreen();
  conn_result = CONN_RUNNING;
  scr_wifi_connecting = buildOverlayScreen();
  setupOrSettingsHeader(scr_wifi_connecting, T(STR_WIFI_TITLE), SETUP_STEP_WIFI,
                        [](lv_event_t *e) { showWifiSetupScreen(); });

  buildConnCard(scr_wifi_connecting);
  char buf[80];
  snprintf(buf, sizeof(buf), T(STR_WIFI_CONNECTING), wifiSetupChosenSsid());
  lv_label_set_text(conn_title, buf);

  // Hidden until there is a result: nothing to answer while it runs.
  conn_bar = setupActionBar(scr_wifi_connecting,
    T(STR_BTN_WIFI_CHANGE), [](lv_event_t *e) {
      logSD("BTN: Change WiFi -> WifiSetup");
      showWifiSetupScreen();
    },
    T(STR_BTN_NEXT), onConnMain);
  lv_obj_add_flag(conn_bar.quiet, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(conn_bar.main, LV_OBJ_FLAG_HIDDEN);
}
