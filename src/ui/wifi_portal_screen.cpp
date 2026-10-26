#include "wifi_portal_screen.h"

#include <Arduino.h>
#include <lvgl.h>
#include <cstring>

#include "app/app_state.h"
#include "extra/libs/qrcode/lv_qrcode.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "navigation.h"
#include "services/setup_portal.h"
#include "ui_common.h"
#include "web/web_server.h"
#include "wifi_setup_screen.h"
#include "theme.h"

// Two cards, one per step: a numbered title, the QR code in a white frame,
// and what to type if the camera is not used. The frame is the quiet zone a
// phone camera needs on the dark screen; the code fills its canvas almost to
// the edge. LVGL scales a code by whole pixels per module, so 120 is kept:
// a step smaller would shrink the drawn code by a whole module size.
#define PORTAL_QR_SIZE     120
#define PORTAL_QR_FRAME    6
#define PORTAL_CARD_Y      52
#define PORTAL_CARD_W      222
#define PORTAL_CARD_H      212
#define PORTAL_CARD_X2     246
#define PORTAL_NUM_PX      22
#define PORTAL_QR_Y        34    // inside the card
#define PORTAL_LINE1_Y     170
#define PORTAL_LINE2_Y     189
#define PORTAL_HINT_Y      268
#define PORTAL_STATUS_Y    290
#define PORTAL_TEXT_W      440

static lv_obj_t *lbl_portal_status   = nullptr;
static bool portal_start_pending     = false;
static bool portal_close_pending     = false;
// The codes are on screen, so a portal that is gone has to take them along.
static bool portal_up_shown          = false;
static bool portal_received_shown    = false;

static lv_obj_t *portalLabel(lv_obj_t *parent, const char *text, const lv_font_t *font,
                             uint32_t color, lv_coord_t y) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
  return l;
}

static void portalQr(lv_obj_t *card, const char *data) {
  lv_obj_t *frame = lv_obj_create(card);
  lv_obj_set_size(frame, PORTAL_QR_SIZE + 2 * PORTAL_QR_FRAME, PORTAL_QR_SIZE + 2 * PORTAL_QR_FRAME);
  lv_obj_align(frame, LV_ALIGN_TOP_MID, 0, PORTAL_QR_Y);
  lv_obj_set_style_bg_color(frame, lv_color_hex(UI_COL_QR_LIGHT), 0);
  lv_obj_set_style_border_width(frame, 0, 0);
  lv_obj_set_style_radius(frame, UI_RADIUS_BTN, 0);
  lv_obj_set_style_pad_all(frame, PORTAL_QR_FRAME, 0);
  lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *qr = lv_qrcode_create(frame, PORTAL_QR_SIZE, lv_color_hex(UI_COL_QR_DARK), lv_color_hex(UI_COL_QR_LIGHT));
  if (!qr) {
    logSD("Portal: no room for a QR code in the LVGL pool");
    return;
  }
  if (lv_qrcode_update(qr, data, strlen(data)) != LV_RES_OK) {
    logSD("Portal: QR code update failed");
  }
  lv_obj_center(qr);
}

// A step's card: the number in a green disc and the step's name beside it.
static lv_obj_t *portalCard(lv_coord_t x, const char *num, int title_id) {
  lv_obj_t *card = lv_obj_create(scr_wifi_portal);
  lv_obj_set_size(card, PORTAL_CARD_W, PORTAL_CARD_H);
  lv_obj_set_pos(card, x, PORTAL_CARD_Y);
  lv_obj_set_style_bg_color(card, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(UI_COL_LINE_SOFT), 0);
  lv_obj_set_style_radius(card, UI_RADIUS_BOX, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *disc = lv_obj_create(card);
  lv_obj_remove_style_all(disc);
  lv_obj_set_size(disc, PORTAL_NUM_PX, PORTAL_NUM_PX);
  lv_obj_set_pos(disc, 12, 6);
  lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(disc, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
  lv_obj_t *n = lv_label_create(disc);
  lv_label_set_text(n, num);
  lv_obj_set_style_text_font(n, UI_FONT_CAPTION, 0);
  lv_obj_set_style_text_color(n, lv_color_hex(UI_COL_ON_ACCENT), 0);
  lv_obj_center(n);

  lv_obj_t *t = lv_label_create(card);
  lv_label_set_text(t, T(title_id));
  lv_obj_set_style_text_font(t, UI_FONT_SMALL, 0);
  lv_obj_set_style_text_color(t, lv_color_hex(UI_COL_INK), 0);
  lv_obj_align_to(t, disc, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
  return card;
}

// The status under the cards as a pill: amber while it waits, green once the
// phone has sent the form.
static void portalStatusPill(uint32_t ink, uint32_t bg, uint32_t line) {
  if (!lbl_portal_status) return;
  lv_obj_set_style_text_color(lbl_portal_status, lv_color_hex(ink), 0);
  lv_obj_set_style_bg_color(lbl_portal_status, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(lbl_portal_status, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(lbl_portal_status, 1, 0);
  lv_obj_set_style_border_color(lbl_portal_status, lv_color_hex(line), 0);
  lv_obj_set_style_radius(lbl_portal_status, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_hor(lbl_portal_status, 14, 0);
  lv_obj_set_style_pad_ver(lbl_portal_status, 4, 0);
}

static void buildWifiPortalScreen() {
  logSD("BUILD: WifiPortalScreen");
  releaseScreen(&scr_wifi_portal);
  scr_wifi_portal = lv_obj_create(lv_scr_act());
  lv_obj_set_size(scr_wifi_portal, 480, 320);
  lv_obj_set_pos(scr_wifi_portal, 0, 0);
  lv_obj_add_flag(scr_wifi_portal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_radius(scr_wifi_portal, 0, 0);
  lv_obj_set_style_border_width(scr_wifi_portal, 0, 0);
  lv_obj_set_style_pad_all(scr_wifi_portal, 0, 0);
  lv_obj_clear_flag(scr_wifi_portal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(scr_wifi_portal, lv_color_hex(UI_COL_GROUND), 0);

  portalLabel(scr_wifi_portal, T(STR_PORTAL_TITLE), &lv_font_montserrat_ext_18, UI_COL_ACCENT, 14);
  // Parked for the loop: going back takes the access point down, and the
  // screen this button sits on is deleted on the way.
  addBackButton(scr_wifi_portal, [](lv_event_t *e) { portal_close_pending = true; });
  addCloseButton(scr_wifi_portal);

  // In the middle while the scan runs; moves under the codes once they exist.
  lbl_portal_status = lv_label_create(scr_wifi_portal);
  lv_label_set_text(lbl_portal_status, T(STR_WIFI_SCAN));
  lv_obj_set_style_text_color(lbl_portal_status, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_style_text_font(lbl_portal_status, &lv_font_montserrat_ext_16, 0);
  lv_obj_set_style_text_align(lbl_portal_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lbl_portal_status, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_portal_status, PORTAL_TEXT_W);
  lv_obj_align(lbl_portal_status, LV_ALIGN_CENTER, 0, 0);
}

static void fillWifiPortalScreen() {
  // Nothing to fill without the screen. A label created on a null parent is
  // a screen of its own to LVGL, never shown and never freed.
  if (!scr_wifi_portal) return;
  char buf[96];
  char url[32];
  setupPortalUrl(url, sizeof(url));

  // Step 1: join the scale's network. The standard WiFi QR payload; neither
  // the generated name nor the generated password has a character it would
  // need escaped.
  lv_obj_t *join = portalCard(12, "1", STR_PORTAL_STEP_JOIN);
  snprintf(buf, sizeof(buf), "WIFI:T:WPA;S:%s;P:%s;;", setupPortalSsid(), setupPortalPassword());
  portalQr(join, buf);
  snprintf(buf, sizeof(buf), T(STR_PORTAL_NET_FMT), setupPortalSsid());
  portalLabel(join, buf, &lv_font_montserrat_ext_14, UI_COL_INK_2, PORTAL_LINE1_Y);
  snprintf(buf, sizeof(buf), T(STR_PORTAL_PASS_FMT), setupPortalPassword());
  portalLabel(join, buf, &lv_font_montserrat_ext_16, UI_COL_INK_MAX, PORTAL_LINE2_Y);

  // Step 2: the page, for a phone that does not open it by itself.
  lv_obj_t *open = portalCard(PORTAL_CARD_X2, "2", STR_PORTAL_STEP_OPEN);
  portalQr(open, url);
  portalLabel(open, url, &lv_font_montserrat_ext_14, UI_COL_ACCENT, PORTAL_LINE1_Y);
  portalLabel(open, T(STR_PORTAL_OPENS_ITSELF), &lv_font_montserrat_ext_12, UI_COL_CAPTION, PORTAL_LINE2_Y + 3);

  lv_obj_t *hint = portalLabel(scr_wifi_portal, T(STR_PORTAL_ANDROID_HINT), &lv_font_montserrat_ext_12,
                               UI_COL_CAPTION, PORTAL_HINT_Y);
  lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(hint, PORTAL_TEXT_W);
  lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, PORTAL_HINT_Y);

  if (lbl_portal_status) {
    lv_label_set_text(lbl_portal_status, T(STR_PORTAL_WAITING));
    lv_obj_set_style_text_font(lbl_portal_status, &lv_font_montserrat_ext_12, 0);
    lv_obj_set_width(lbl_portal_status, LV_SIZE_CONTENT);
    portalStatusPill(UI_COL_WARN, UI_COL_AMBER_BG, UI_COL_AMBER_LINE);
    lv_obj_align(lbl_portal_status, LV_ALIGN_TOP_MID, 0, PORTAL_STATUS_Y);
  }
}

void showWifiPortalScreen() {
  logSD("SHOW: WifiPortalScreen");
  logSD("UI: Screen -> WifiPortal");
  hideAllOverlays();
  closeWifiPortalScreen();
  buildWifiPortalScreen();
  lv_obj_clear_flag(scr_wifi_portal, LV_OBJ_FLAG_HIDDEN);
  // The scan takes seconds and belongs in the loop, not in the button.
  portal_start_pending = true;
}

void closeWifiPortalScreen() {
  lbl_portal_status     = nullptr;
  portal_start_pending  = false;
  portal_up_shown       = false;
  portal_received_shown = false;
  releaseScreen(&scr_wifi_portal);
}

void handleWifiPortalDeferredActions() {
  if (portal_close_pending) {
    portal_close_pending = false;
    setupPortalStop();
    webServerSyncState();
    closeWifiPortalScreen();
    showWifiSetupScreen();
    return;
  }

  const bool visible = scr_wifi_portal && !lv_obj_has_flag(scr_wifi_portal, LV_OBJ_FLAG_HIDDEN);

  if (portal_start_pending) {
    portal_start_pending = false;
    if (!visible) return;
    lv_timer_handler();   // the scan line is on screen before the scan blocks
    // That pass can have dispatched the close button, which takes this screen
    // down through showMainScreen(). Sampled again rather than trusted from
    // above the handler.
    if (!scr_wifi_portal) return;
    if (setupPortalStart()) {
      webServerSyncState();
      fillWifiPortalScreen();
      portal_up_shown = true;
    } else if (lbl_portal_status) {
      lv_label_set_text(lbl_portal_status, T(STR_PORTAL_START_FAILED));
      lv_obj_set_style_text_color(lbl_portal_status, lv_color_hex(UI_COL_BAD_TEXT), 0);
    }
    return;
  }

  // The form was sent and the access point has closed: on to the connect,
  // exactly as if the password had been typed here.
  char ssid[33];
  char pass[65];
  if (setupPortalTakeCredentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
    webServerSyncState();
    closeWifiPortalScreen();
    wifiSetupConnectWith(ssid, pass);
    memset(pass, 0, sizeof(pass));
    return;
  }

  if (visible && setupPortalSubmitted() && !portal_received_shown && lbl_portal_status) {
    portal_received_shown = true;
    lv_label_set_text(lbl_portal_status, T(STR_PORTAL_RECEIVED));
    portalStatusPill(UI_COL_GOOD, UI_COL_OK_BG, UI_COL_GO_BG_PRESSED);
  }

  // Whatever took the screen away also takes the access point down.
  if (setupPortalActive() && !visible) {
    setupPortalStop();
    webServerSyncState();
    return;
  }
  // And the other way round: Improv ended the portal under the open screen.
  if (visible && portal_up_shown && !setupPortalActive()) {
    closeWifiPortalScreen();
    showWifiSetupScreen();
  }
}
