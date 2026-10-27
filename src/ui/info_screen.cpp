#include "info_screen.h"
#include "navigation.h"
#include "app/app_state.h"
#include "app/deferred_actions.h"

#include <Arduino.h>
#include <lvgl.h>

#include "app_config.h"
#include "hardware/sd_logger.h"
#include "icons.h"
#include "lang.h"
#include "ui/theme.h"
#include "ui_common.h"
#include "extra/libs/qrcode/lv_qrcode.h"



// The manual is last in the arrays and first on the screen: most questions
// people ask are answered there (Nikolai, 25.09.2026).
#define QR_COUNT 6
// Not a tile here: the Bluetooth screen opens it (QR_PRINTERS in info_screen.h).
static_assert(QR_DOCS == 4, "QR_DOCS is the manual's entry of the arrays below");
static_assert(QR_PRINTERS == 5, "QR_PRINTERS is the last entry of the arrays below");
static const char* QR_TITLES[QR_COUNT] = { "Ko-fi", "GitHub", "Discord", "MakerWorld", nullptr, nullptr };
static const char* QR_URLS[QR_COUNT]   = {
  "https://ko-fi.com/formfollowsfunction",
  "https://github.com/" FW_REPO,   // the fork the updates come from
  "https://discord.gg/K3KmKnrWhP",
  "https://makerworld.com/de/@FormFollowsF/upload",
  "https://niko11111.github.io/SpoolmanScale-Docs/",
  nullptr   // by language, see qrUrl()
};
static const char* QR_URLS_DISPLAY[QR_COUNT] = {
  "ko-fi.com/formfollowsfunction",
  "github.com/" FW_REPO,
  "discord.gg/K3KmKnrWhP",
  "makerworld.com/de/@FormFollowsF/upload",
  "niko11111.github.io/SpoolmanScale-Docs",
  nullptr
};

// The pages a swipe walks through, in the order of the overview: the manual,
// then the four tiles row by row. The printers page stands alone.
static const int QR_PAGE_ORDER[] = { QR_DOCS, 0, 1, 2, 3 };
static constexpr int QR_PAGE_COUNT = sizeof(QR_PAGE_ORDER) / sizeof(QR_PAGE_ORDER[0]);

// The overview: the manual across the top, the four others two by two.
static constexpr int INFO_X0         = 12;
static constexpr int INFO_TILE_GAP   = 12;
static constexpr int INFO_TILE_W     = (480 - 2 * INFO_X0 - INFO_TILE_GAP) / 2;
static constexpr int INFO_TILE_H     = 60;
static constexpr int INFO_DOCS_Y     = 90;
static constexpr int INFO_ROW_STEP   = INFO_TILE_H + 8;
static constexpr int INFO_DISC_D     = 34;
static constexpr int INFO_TEXT_X     = 56;   // the name and its line, right of the disc

// A QR page: the code in a white frame on the left, what it leads to on the right.
static constexpr int QR_FRAME_X      = 16;
static constexpr int QR_FRAME_Y      = 60;
static constexpr int QR_FRAME_PX     = 200;
static constexpr int QR_CODE_PX      = 180;
static constexpr int QR_COL_X        = 232;
static constexpr int QR_COL_W        = 232;
static constexpr int QR_DISC_D       = 40;
static constexpr int QR_SCAN_Y       = 236;
static constexpr int QR_DOT_PX       = 8;
static constexpr int QR_DOTS_Y       = 274;

// The page on screen, a child of the active screen so the Bluetooth screen can
// open one too. Null when none is.
static lv_obj_t* qr_page = nullptr;
static int       qr_page_idx = -1;

static const char* qrTitle(int idx) {
  if (idx == QR_DOCS) return T(STR_QR_DOCS_TITLE);
  if (idx == QR_PRINTERS) return T(STR_W_P_SHOP_TITLE);
  return QR_TITLES[idx];
}

// The manual has a German page and an English one; French readers get the
// English, as in the browser's link.
static const char* qrUrl(int idx) {
  if (idx != QR_PRINTERS) return QR_URLS[idx];
  return g_lang == LANG_DE ? DOCS_URL "de/use/printers/" : DOCS_URL "use/printers/";
}

// What stands under the title, the address without its scheme.
static const char* qrUrlDisplay(int idx) {
  return idx == QR_PRINTERS ? qrUrl(idx) + strlen("https://") : QR_URLS_DISPLAY[idx];
}

static const char* getQRDesc(int idx) {
  switch(idx) {
    case 0: return T(STR_QR_KOFI_DESC);
    case 1: return T(STR_QR_GITHUB_DESC);
    case 2: return T(STR_QR_DISCORD_DESC);
    case 3: return T(STR_QR_MAKER_DESC);
    case QR_DOCS: return T(STR_QR_DOCS_DESC);
    case QR_PRINTERS: return T(STR_W_P_SHOP_HINT);
    default: return "";
  }
}

// Each target's icon and colours, the same on its tile and on its page.
struct QrLook { const lv_img_dsc_t* icon; uint32_t bg; uint32_t ink; };

static QrLook qrLook(int idx) {
  switch (idx) {
    case 0:  return { &ICON_CUP,  UI_COL_KOFI_BG,       UI_COL_KOFI_INK };
    case 1:  return { &ICON_CODE, UI_COL_RULE,          UI_COL_INK };
    case 2:  return { &ICON_CHAT, UI_COL_DISCORD_BG,    UI_COL_DISCORD_INK };
    case 3:  return { &ICON_CUBE, UI_COL_MAKERWORLD_BG, UI_COL_MAKERWORLD_INK };
    default: return { &ICON_BOOK, UI_COL_GO_BG,         UI_COL_ACCENT };
  }
}

static int qrPagePos(int idx) {
  for (int i = 0; i < QR_PAGE_COUNT; i++) {
    if (QR_PAGE_ORDER[i] == idx) return i;
  }
  return -1;
}

// Async, because every caller is a handler of the page or of a button on it.
static void closeQrPage() {
  if (qr_page) lv_obj_del_async(qr_page);
  qr_page = nullptr;
  qr_page_idx = -1;
}

// The X closes more than showMainScreen() knows of: the page and the overview
// both sit on the active screen outside its list of overlays.
static void onInfoClose(lv_event_t* e) {
  closeQrPage();
  if (scr_info) lv_obj_del_async(scr_info);
  scr_info = nullptr;
}

static lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t col) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
  return l;
}

static void onQrSwipe(lv_event_t* e) {
  const lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
  if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) return;
  const int pos = qrPagePos(qr_page_idx);
  if (pos < 0) return;
  const int step = dir == LV_DIR_LEFT ? 1 : QR_PAGE_COUNT - 1;
  // The finger is still down: without this it would press whatever of the
  // new page lies under it.
  lv_indev_wait_release(lv_indev_get_act());
  showQRPopup(QR_PAGE_ORDER[(pos + step) % QR_PAGE_COUNT]);
}

static void qrPageDots(lv_obj_t* page, int idx) {
  const int pos = qrPagePos(idx);
  if (pos < 0) return;
  const int row_w = QR_PAGE_COUNT * QR_DOT_PX + (QR_PAGE_COUNT - 1) * QR_DOT_PX;
  for (int i = 0; i < QR_PAGE_COUNT; i++) {
    lv_obj_t* d = lv_obj_create(page);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, QR_DOT_PX, QR_DOT_PX);
    lv_obj_set_pos(d, (480 - row_w) / 2 + i * 2 * QR_DOT_PX, QR_DOTS_Y);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(i == pos ? UI_COL_ACCENT : UI_COL_LINE), 0);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE);
  }
}

static void qrPageCode(lv_obj_t* page, int idx) {
  lv_obj_t* frame = lv_obj_create(page);
  lv_obj_set_size(frame, QR_FRAME_PX, QR_FRAME_PX);
  lv_obj_set_pos(frame, QR_FRAME_X, QR_FRAME_Y);
  lv_obj_set_style_bg_color(frame, lv_color_hex(UI_COL_QR_LIGHT), 0);
  lv_obj_set_style_border_width(frame, 0, 0);
  lv_obj_set_style_radius(frame, UI_RADIUS_BOX, 0);
  lv_obj_set_style_pad_all(frame, 0, 0);
  lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

  const char* url = qrUrl(idx);
  lv_obj_t* qr = lv_qrcode_create(frame, QR_CODE_PX, lv_color_hex(UI_COL_QR_DARK), lv_color_hex(UI_COL_QR_LIGHT));
  if (!qr) {
    logSD("QR: no room for the code in the LVGL pool");
    return;
  }
  if (lv_qrcode_update(qr, url, strlen(url)) != LV_RES_OK) logSDf("QR: update failed for %s", url);
  lv_obj_center(qr);
}

static void qrPageText(lv_obj_t* page, int idx) {
  const QrLook look = qrLook(idx);
  lv_obj_t* disc = uiIconDisc(page, look.icon, QR_DISC_D, look.bg, look.ink);
  lv_obj_set_pos(disc, QR_COL_X, QR_FRAME_Y + 4);

  // The printers' text runs to three lines; a step smaller keeps the
  // address above the scan line.
  lv_obj_t* desc = label(page, getQRDesc(idx), idx == QR_PRINTERS ? UI_FONT_SMALL : UI_FONT_BODY, UI_COL_INK);
  lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(desc, QR_COL_W);
  lv_obj_set_pos(desc, QR_COL_X, QR_FRAME_Y + QR_DISC_D + 16);

  lv_obj_t* url = label(page, qrUrlDisplay(idx), UI_FONT_SMALL, UI_COL_ACCENT);
  lv_label_set_long_mode(url, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(url, QR_COL_W);
  lv_obj_align_to(url, desc, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

  lv_obj_t* scan = label(page, T(STR_QR_SCAN_HINT), UI_FONT_CAPTION, UI_COL_CAPTION);
  lv_label_set_long_mode(scan, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(scan, QR_COL_W);
  lv_obj_set_pos(scan, QR_COL_X, QR_SCAN_Y);
}

void showQRPopup(int idx) {
  if (idx < 0 || idx >= QR_COUNT) return;

  logSDf("SHOW: QRPopup idx=%d", idx);
  closeQrPage();
  qr_page = lv_obj_create(lv_scr_act());
  qr_page_idx = idx;
  lv_obj_set_size(qr_page, 480, 320);
  lv_obj_set_pos(qr_page, 0, 0);
  lv_obj_set_style_bg_color(qr_page, lv_color_hex(UI_COL_GROUND), 0);
  lv_obj_set_style_border_width(qr_page, 0, 0);
  lv_obj_set_style_radius(qr_page, 0, 0);
  lv_obj_set_style_pad_all(qr_page, 0, 0);
  lv_obj_clear_flag(qr_page, LV_OBJ_FLAG_SCROLLABLE);
  // A sideways drag would otherwise go on to the screen behind as an elastic
  // scroll, and LVGL sends no gesture while something scrolls. The gesture
  // itself stops here instead of rising to the screen.
  lv_obj_clear_flag(qr_page, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
  lv_obj_clear_flag(qr_page, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(qr_page, onQrSwipe, LV_EVENT_GESTURE, NULL);

  addBackButton(qr_page, [](lv_event_t* e) { closeQrPage(); });
  lv_obj_t* title = label(qr_page, qrTitle(idx), UI_FONT_TITLE, UI_COL_ACCENT);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);
  // Opened from the setup as well ("no server yet?"): there the close button
  // would end the setup behind the user's back, so back is the only way out.
  if (!setup_active) lv_obj_add_event_cb(addCloseButton(qr_page), onInfoClose, LV_EVENT_CLICKED, NULL);

  qrPageCode(qr_page, idx);
  qrPageText(qr_page, idx);
  qrPageDots(qr_page, idx);
}

// One target: icon disc, name, and a line on what is behind it. The manual's
// tile spans the screen and carries an arrow and the accent frame.
static void infoTile(lv_obj_t* parent, int x, int y, int w, int idx, int sub_id) {
  const bool wide = idx == QR_DOCS;
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_size(b, w, INFO_TILE_H);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_ROW), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_ROW_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_radius(b, UI_RADIUS_ROW, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(wide ? UI_COL_ACCENT : UI_COL_ROW_PRESSED), 0);

  const QrLook look = qrLook(idx);
  lv_obj_t* disc = uiIconDisc(b, look.icon, INFO_DISC_D, look.bg, look.ink);
  lv_obj_align(disc, LV_ALIGN_LEFT_MID, 12, 0);

  lv_obj_t* name = label(b, qrTitle(idx), UI_FONT_BODY, UI_COL_INK);
  lv_obj_set_pos(name, INFO_TEXT_X, 10);
  lv_obj_t* sub = label(b, T(sub_id), UI_FONT_CAPTION, UI_COL_CAPTION);
  lv_obj_set_pos(sub, INFO_TEXT_X, 33);

  if (wide) {
    lv_obj_t* arrow = label(b, LV_SYMBOL_RIGHT, UI_FONT_SMALL, UI_COL_INK_FAINT);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -16, 0);
  }

  lv_obj_add_event_cb(b, [](lv_event_t* e) {
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    logSDf("BTN: Info -> QR %d", i);
    showQRPopup(i);
  }, LV_EVENT_CLICKED, (void*)(intptr_t)idx);
}

// "SpoolmanScale" and the version in a green chip, centred as a pair.
static void infoVersionLine(lv_obj_t* parent) {
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 8, 0);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 58);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

  label(row, T(STR_INFO_PRODUCT), UI_FONT_SMALL, UI_COL_INK);
  lv_obj_t* chip = label(row, FW_VERSION, UI_FONT_CAPTION, UI_COL_ACCENT);
  lv_obj_set_style_bg_color(chip, lv_color_hex(UI_COL_ACCENT_CHIP), 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_hor(chip, 9, 0);
  lv_obj_set_style_pad_ver(chip, 3, 0);
}

void showInfoScreen() {
  logSD("SHOW: InfoScreen");
  logSD("UI: Screen -> Info");
  if (scr_info) { lv_obj_del(scr_info); scr_info = nullptr; }
  scr_info = lv_obj_create(lv_scr_act());
  lv_obj_set_size(scr_info, 480, 320);
  lv_obj_set_pos(scr_info, 0, 0);
  lv_obj_set_style_bg_color(scr_info, lv_color_hex(UI_COL_GROUND), 0);
  lv_obj_set_style_border_width(scr_info, 0, 0);
  lv_obj_set_style_radius(scr_info, 0, 0);
  lv_obj_set_style_pad_all(scr_info, 0, 0);
  lv_obj_clear_flag(scr_info, LV_OBJ_FLAG_SCROLLABLE);

  addBackButton(scr_info, [](lv_event_t *e){
    logSD("BTN: Info -> Back");
    show_system_pending = true;
  });
  lv_obj_t* hdr = label(scr_info, T(STR_BTN_INFO), UI_FONT_TITLE, UI_COL_ACCENT);
  lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_add_event_cb(addCloseButton(scr_info), onInfoClose, LV_EVENT_CLICKED, NULL);

  infoVersionLine(scr_info);

  const int x1 = INFO_X0 + INFO_TILE_W + INFO_TILE_GAP;
  const int y1 = INFO_DOCS_Y + INFO_ROW_STEP, y2 = y1 + INFO_ROW_STEP;
  infoTile(scr_info, INFO_X0, INFO_DOCS_Y, 480 - 2 * INFO_X0, QR_DOCS, STR_QR_DOCS_DESC);
  infoTile(scr_info, INFO_X0, y1, INFO_TILE_W, 0, STR_INFO_SUB_KOFI);
  infoTile(scr_info, x1,      y1, INFO_TILE_W, 1, STR_INFO_SUB_GITHUB);
  infoTile(scr_info, INFO_X0, y2, INFO_TILE_W, 2, STR_INFO_SUB_DISCORD);
  infoTile(scr_info, x1,      y2, INFO_TILE_W, 3, STR_INFO_SUB_MAKER);

  // The disclaimer only existed in the web interface and the README, so
  // anyone who never opened either never saw it. The device is called
  // SpoolmanScale, which is exactly why it needs to say this somewhere the
  // user actually looks. Both names appear regardless of the active backend.
  lv_obj_t *disc = label(scr_info, T(STR_NOT_AFFILIATED), &lv_font_montserrat_ext_10, UI_COL_INK_FAINT);
  lv_obj_set_style_text_align(disc, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(disc, LV_ALIGN_BOTTOM_MID, 0, -6);
}
