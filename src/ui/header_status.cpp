#include "header_status.h"
#include "app/app_state.h"
#include "boards/board.h"
#include "services/backend.h"
#include "services/battery.h"
#include "services/ble_service.h"

#include <Arduino.h>
#include <cstring>
#include <lvgl.h>

#include "services/device_name.h"
#include "services/mdns_service.h"
#include "services/user_options.h"
#include "ui/main_screen_helpers.h"
#include "ui/theme.h"
#include "services/wifi_manager.h"


// 192.168.4.100:7913 needs about 113 px in font 12 and the address label is
// 94 wide, so the port mode gets the room it needs and the status line to its
// left gives it up - more again when the address carries "https://". Every
// other mode leaves both as they were.
static void sizeHeaderAddress(const char* text) {
  int ip_w     = HDR_IP_W;
  int status_w = HDR_STATUS_W;
  if (g_ip_bar_mode == IP_BAR_BACKEND_PORT) {
    const bool https = (strncmp(text, "https://", 8) == 0);
    ip_w     = https ? HDR_IP_W_HTTPS     : HDR_IP_W_PORT;
    status_w = https ? HDR_STATUS_W_HTTPS : HDR_STATUS_W_NARROW;
  }
  lv_obj_set_width(lbl_hdr_ip, ip_w);
  if (lbl_status) lv_obj_set_width(lbl_status, status_w);
}

static lv_color_t wifiColor() {
  if (!wifi_ok) return lv_color_hex(UI_COL_BAD);
  int rssi = wifiManagerRSSI();
  if (rssi >= -65) return lv_color_hex(UI_COL_GOOD);
  if (rssi >= -75) return lv_color_hex(UI_COL_WARN);
  return lv_color_hex(UI_COL_SIGNAL_LOW);
}

// Right to left, one gap between neighbours. Fixed offsets were what made the
// spacing uneven: they put the chips on a 32 px pitch while the labels differ
// in width, so the gaps came out 5, 9 and 7 px. Widths also move at runtime -
// "NFC" becomes "NFC!" on an error and the badge follows the backend - which
// is why this runs after every text change rather than once at build time.
#define HDR_CHIP_MARGIN  8
#define HDR_CHIP_GAP    10

void layoutHeaderChips() {
  if (!lbl_hdr_sm) return;
  // A label sized to its content only knows its new width after a layout pass,
  // and lv_obj_align_to() reads that width. Without this the chips would be
  // placed from the previous text.
  lv_obj_update_layout(lv_obj_get_parent(lbl_hdr_sm));

  lv_obj_align(lbl_hdr_sm, LV_ALIGN_RIGHT_MID, -HDR_CHIP_MARGIN, 0);
  lv_obj_t *prev = lbl_hdr_sm;
  // AMS first, so it lands directly left of the backend badge: it belongs to
  // the backend, and the two read as a pair.
  lv_obj_t *chain[] = { btn_hdr_ams, lbl_hdr_scl, btn_hdr_nfc, lbl_hdr_wifi, lbl_hdr_bt, lbl_hdr_sd };
  for (unsigned i = 0; i < sizeof(chain) / sizeof(chain[0]); i++) {
    // Hidden counts as absent. lv_obj_align_to() reads nothing but the
    // reference object's geometry - the hidden flag never reaches it - so a
    // hidden label keeps its slot and leaves a hole in the row. That is what
    // the SD icon has been doing on a card-less device, and it is what a chip
    // that comes and goes at runtime would do on every appearance.
    if (!chain[i] || lv_obj_has_flag(chain[i], LV_OBJ_FLAG_HIDDEN)) continue;
    lv_obj_align_to(chain[i], prev, LV_ALIGN_OUT_LEFT_MID, -HDR_CHIP_GAP, 0);
    prev = chain[i];
  }

  // The address follows the scan counter for the same reason: that counter
  // grows a digit at a time and a fixed offset would eventually collide.
  if (lbl_hdr_ip && lbl_hdr_scans)
    lv_obj_align_to(lbl_hdr_ip, lbl_hdr_scans, LV_ALIGN_OUT_LEFT_MID, -HDR_CHIP_GAP, 0);

  // Last: the battery sits in what the chips leave of the line.
  updateHeaderBattery();
}

#if BOARD_BATTERY_SENSE
// The leftmost visible chip, where the battery's room ends. The backend badge
// is always there; the others count when they are not hidden.
static lv_coord_t firstChipX() {
  lv_coord_t x = lv_obj_get_x(lbl_hdr_sm);
  lv_obj_t *chain[] = { btn_hdr_ams, lbl_hdr_scl, btn_hdr_nfc, lbl_hdr_wifi, lbl_hdr_bt, lbl_hdr_sd };
  for (unsigned i = 0; i < sizeof(chain) / sizeof(chain[0]); i++) {
    if (!chain[i] || lv_obj_has_flag(chain[i], LV_OBJ_FLAG_HIDDEN)) continue;
    const lv_coord_t cx = lv_obj_get_x(chain[i]);
    if (cx < x) x = cx;
  }
  return x;
}

static lv_color_t batteryColor(bat::Tone tone) {
  switch (tone) {
    case bat::TONE_CHARGING: return lv_color_hex(UI_COL_GOOD);
    case bat::TONE_CRITICAL: return lv_color_hex(UI_COL_BAD);
    case bat::TONE_LOW:      return lv_color_hex(UI_COL_WARN);
    default:                 return lv_color_hex(UI_COL_INK_FAINT);
  }
}
#endif

// Symbol and step ("<battery> 70%"), the bolt instead of the battery while
// charging; the symbol alone where the line has no room for the number
// (the acceptance firmware's longer version, Bluetooth and SD shown), and
// nothing where it has none at all. Grey, yellow from 20 %, red below the
// warning voltage, the bolt green (Spec 5.3 AB).
void updateHeaderBattery() {
#if BOARD_BATTERY_SENSE
  if (!lbl_hdr_bat || !lbl_hdr_ver || !lbl_hdr_sm) return;
  const bat::State &s = batteryState();
  const bat::Tone tone = bat::toneOf(s);
  if (tone == bat::TONE_HIDDEN) {
    lv_obj_add_flag(lbl_hdr_bat, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  static const char *const LEVELS[] = { LV_SYMBOL_BATTERY_EMPTY, LV_SYMBOL_BATTERY_1,
                                        LV_SYMBOL_BATTERY_2, LV_SYMBOL_BATTERY_3,
                                        LV_SYMBOL_BATTERY_FULL };
  const uint8_t shown = s.shown < 0 ? 0 : (uint8_t)s.shown;
  const char *symbol = (tone == bat::TONE_CHARGING) ? LV_SYMBOL_CHARGE : LEVELS[bat::iconOf(shown)];
  char full[24];
  snprintf(full, sizeof(full), "%s %u%%", symbol, (unsigned)shown);

  const lv_font_t *font = &lv_font_montserrat_ext_12;
  lv_point_t w_full, w_symbol;
  lv_txt_get_size(&w_full, full, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  lv_txt_get_size(&w_symbol, symbol, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  lv_obj_update_layout(lv_obj_get_parent(lbl_hdr_ver));
  const lv_coord_t left_end = lv_obj_get_x(lbl_hdr_ver) + lv_obj_get_width(lbl_hdr_ver);
  const bat::Slot slot = bat::headerSlot((int16_t)left_end, (int16_t)firstChipX(),
                                         (int16_t)w_full.x, (int16_t)w_symbol.x);
  if (slot.form == bat::FORM_NONE) {
    lv_obj_add_flag(lbl_hdr_bat, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_label_set_text(lbl_hdr_bat, slot.form == bat::FORM_FULL ? full : symbol);
  lv_obj_set_style_text_color(lbl_hdr_bat, batteryColor(tone), 0);
  lv_obj_align(lbl_hdr_bat, LV_ALIGN_LEFT_MID, slot.x, 0);
  lv_obj_clear_flag(lbl_hdr_bat, LV_OBJ_FLAG_HIDDEN);
#endif
}

void updateHeaderStatus() {
  if (!lbl_hdr_wifi) return;

  lv_obj_set_style_text_color(lbl_hdr_wifi, wifiColor(), 0);

  // Bluetooth follows the master switch: shown while BLE may be used, gone
  // otherwise. The chip chain leaves no hole for a hidden one.
  if (lbl_hdr_bt) {
    if (bleEnabled()) lv_obj_clear_flag(lbl_hdr_bt, LV_OBJ_FLAG_HIDDEN);
    else              lv_obj_add_flag(lbl_hdr_bt, LV_OBJ_FLAG_HIDDEN);
  }

  if (lbl_hdr_nfc) {
    lv_label_set_text(lbl_hdr_nfc, nfc_ok ? "NFC" : "NFC!");
    const lv_color_t c = lv_color_hex(nfc_ok ? UI_COL_GOOD : UI_COL_BAD);
    lv_obj_set_style_text_color(lbl_hdr_nfc, c, 0);
    // The chip's frame follows its label, so a fault shows on the button too.
    if (btn_hdr_nfc) lv_obj_set_style_border_color(btn_hdr_nfc, c, 0);
  }

  // The chip is not built at all when the device starts without a load cell.
  // It still exists when the switch is thrown while the device is running, and
  // then it has to go: the readings stop, so whatever it last said is frozen.
  // The restart the switch asks for rebuilds the header without it.
  if (lbl_hdr_scl) {
    if (!g_scale_fitted) {
      lv_obj_add_flag(lbl_hdr_scl, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(lbl_hdr_scl, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(lbl_hdr_scl, scl_ok ? "SCL" : "SCL!");
      lv_obj_set_style_text_color(lbl_hdr_scl,
        scl_ok ? lv_color_hex(UI_COL_GOOD) : lv_color_hex(UI_COL_BAD), 0);
    }
  }

  // Both labels follow the active backend. Set here rather than only at build
  // time, because the backend can be switched while the main screen exists.
  if (lbl_hdr_sm) {
    lv_label_set_text(lbl_hdr_sm, backendBadge());
    lv_obj_set_style_text_color(lbl_hdr_sm,
      sm_reachable ? lv_color_hex(UI_COL_GOOD) : lv_color_hex(UI_COL_BAD), 0);
  }

  if (lbl_sm_cap) {
    char cap_buf[16];
    backendCaption(cap_buf, sizeof(cap_buf));
    lv_label_set_text(lbl_sm_cap, cap_buf);
  }

  if (lbl_hdr_scans) {
    char buf[12];
    snprintf(buf, sizeof(buf), "#%d", scan_count);
    lv_label_set_text(lbl_hdr_scans, buf);
  }

  // Status bar address. Device mode needs a live link to mean anything, but
  // the backend address stays correct while offline - that something is wrong
  // is already said by the red SPM/FLM above it.
  if (lbl_hdr_ip) {
    const char *text = nullptr;
    String ip;
    char host_buf[48];
    if (g_ip_bar_mode == IP_BAR_DEVICE && wifi_ok) {
      ip = wifiManagerLocalIP().toString();
      text = ip.c_str();
    } else if (g_ip_bar_mode == IP_BAR_MDNS && mdnsRunning()) {
      // Bare label, no suffix. Same reasoning as the port below: there are
      // about 94 pixels between the status text and the scan counter, and
      // the suffix is the same on every device, so it only eats width.
      text = deviceLabel();
    } else if (g_ip_bar_mode == IP_BAR_BACKEND) {
      // Address only, no port. The point here is telling one server from
      // another when several are around - a test instance next to the live
      // one - and the port is the same on all of them, so it only eats width.
      const char *h = backendHost();
      if (h && h[0]) {
        const char *colon = strchr(h, ':');
        if (colon) {
          size_t n = (size_t)(colon - h);
          if (n >= sizeof(host_buf)) n = sizeof(host_buf) - 1;
          memcpy(host_buf, h, n);
          host_buf[n] = '\0';
          text = host_buf;
        } else {
          text = h;
        }
      }
    } else if (g_ip_bar_mode == IP_BAR_BACKEND_PORT) {
      // As the user typed it, port included. backendHost() already holds
      // "ip:port" when one was given - the mode above is the one that takes
      // it apart, this one does not.
      const char *h = backendHost();
      if (h && h[0]) text = h;
    }
    if (text) {
      sizeHeaderAddress(text);
      lv_label_set_text(lbl_hdr_ip, text);
      lv_obj_clear_flag(lbl_hdr_ip, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(lbl_hdr_ip, LV_OBJ_FLAG_HIDDEN);
      if (lbl_status) lv_obj_set_width(lbl_status, HDR_STATUS_W);
    }
  }

  // Zone 4's AMS button hangs on the backend, and this function already runs
  // on every backend switch - backendApplyMode() calls it. Costs two pointer
  // checks on a device that has a scale, where neither object exists.
  updateAmsAffordance();

  // Last: every text above is final by now, so the widths the packing reads
  // are the ones that will actually be drawn.
  layoutHeaderChips();
}
