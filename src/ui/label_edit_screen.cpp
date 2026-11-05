#include "ui/label_edit_screen.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "app/deferred_actions.h"
#include "app/label_spool.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/label_edit_rules.h"
#include "services/label_facts.h"
#include "services/label_printer.h"
#include "services/label_render.h"
#include "services/label_thumb.h"
#include "services/spool_link_store.h"
#include "ui/key_maps.h"
#include "ui/navigation.h"
#include "ui/printer_screen.h"
#include "ui/theme.h"
#include "ui_common.h"

// The screen, 480 x 320 under the sub header: the label on white paper on
// the left with the print button under it, the rows on the right.
#define LE_TOP          58
#define LE_LEFT_X        8
#define LE_LEFT_W      216
#define LE_PAPER_H     190
#define LE_PAPER_PAD     6
#define LE_THUMB_W     (LE_LEFT_W - 2 * LE_PAPER_PAD)
#define LE_THUMB_H     (LE_PAPER_H - 2 * LE_PAPER_PAD)
#define LE_BTN_Y       (LE_TOP + LE_PAPER_H + 6)
#define LE_BTN_H        54
#define LE_LIST_X      (LE_LEFT_X + LE_LEFT_W + 4)
#define LE_LIST_W      (480 - LE_LIST_X)
#define LE_ROW_W       (LE_LIST_W - 16)
#define LE_ROW_H        50
#define LE_TEXT_W      (LE_ROW_W - 56)

// Row ids beyond the rows of a label (0 .. LABEL_EDIT_ROWS - 1).
#define LE_ROW_CODE       100   // the code on or off
#define LE_ROW_CODE_TEXT  101   // a free label's code text
#define LE_ROW_RESET      102   // a spool's label as the template, a free one empty
#define LE_ROW_FREE_TEXT  103   // the template's free line, its text
#define LE_ROW_FIELD      200   // the template's switches: 200 + FIELDS index

enum EditMode : uint8_t { EM_NONE = 0, EM_SPOOL, EM_TEMPLATE, EM_FREE };

// What the screen works on, in PSRAM while it is open. The free label's rows
// are kept apart, from the first free label to the next restart.
struct EditState {
  EditMode mode;
  LabelKind kind;
  SpoolLabelData spool;
  LabelEdit edit;
};
static EditState* s_st = nullptr;
static LabelEdit* s_free = nullptr;

static lv_obj_t* s_scr = nullptr;
static lv_obj_t* s_kb_scr = nullptr;
static lv_obj_t* s_ta = nullptr;
static lv_obj_t* s_img = nullptr;
static lv_obj_t* s_msg = nullptr;
// The picture LVGL draws: an 8-bit coverage map in PSRAM, as on the sheet.
static lv_img_dsc_t s_dsc{};
static uint8_t* s_px = nullptr;

// What a tap parked for the loop.
static EditMode s_want = EM_NONE;
static LabelKind s_want_kind = LABEL_KIND_SPOOL;
static int  s_tap = -1;
static int  s_typing = -1;      // the row the keyboard writes into
static bool s_typed = false;
static bool s_kb_leave = false;
static bool s_print = false;
static bool s_back = false;

// The switches of the template, in the order of the label page in the
// browser (web/pages/page_label.cpp FIELD_ROWS), with its captions.
struct FieldRow { LabelField field; int caption; };
static const FieldRow FIELDS[] = {
  { LF_VENDOR,   STR_LBL_VENDOR },
  { LF_MATERIAL, STR_LBL_MATERIAL },
  { LF_NAME,     STR_W_L_F_NAME },
  { LF_SPOOL_ID, STR_W_TAG_SPOOLID },
  { LF_COLOR,    STR_LBL_L_COLOR },
  { LF_COLOR_RGB, STR_LBL_F_COLOR_RGB },
  { LF_ARTICLE,  STR_LBL_ARTICLE_NO_SHORT },
  { LF_DATE,     STR_W_L_F_DATE },
  { LF_SPOOL_WEIGHT, STR_W_L_F_SPOOL_W },
  { LF_NET_WEIGHT, STR_W_L_F_NET_W },
  { LF_TEMPS,    STR_W_L_F_TEMPS },
  { LF_DRYING,   STR_W_L_F_DRYING },
  { LF_LOCATION, STR_W_L_F_LOCATION },
  { LF_DRIED,    STR_W_L_F_DRIED },
  { LF_FREE_TEXT, STR_W_L_F_TEXT },
  { LF_QR,       STR_W_L_F_QR },
  { LF_BRAND,    STR_W_L_F_BRAND },
};
static const int FIELD_COUNT = sizeof(FIELDS) / sizeof(FIELDS[0]);

bool labelEditOpen() { return s_scr != nullptr; }

void labelEditOpenSpool(LabelKind kind)    { s_want = EM_SPOOL;    s_want_kind = kind; }
void labelEditOpenTemplate(LabelKind kind) { s_want = EM_TEMPLATE; s_want_kind = kind; }
void labelEditOpenFree()                   { s_want = EM_FREE;     s_want_kind = LABEL_KIND_SPOOL; }

// ---- the picture -------------------------------------------------------

static void freePicture() {
  if (s_img) lv_img_set_src(s_img, nullptr);
  lv_img_cache_invalidate_src(&s_dsc);
  if (s_px) heap_caps_free(s_px);
  s_px = nullptr;
  s_dsc = lv_img_dsc_t{};
}

static void showMessage(int text) {
  freePicture();
  if (!s_img || !s_msg) return;
  lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(s_msg, T(text));
}

static void showPicture(const LabelRaster& r, const LabelThumbSpec& spec) {
  uint16_t w = 0, h = 0;
  labelThumbSize(r, spec, &w, &h);
  freePicture();
  s_px = w && h ? (uint8_t*)heap_caps_malloc(size_t(w) * h, MALLOC_CAP_SPIRAM) : nullptr;
  if (!s_px) { logSD("Label edit: no PSRAM for the preview"); showMessage(STR_W_L_PREVIEW_FAIL); return; }
  labelThumbFill(r, spec, w, h, s_px);
  s_dsc.header.cf = LV_IMG_CF_ALPHA_8BIT;
  s_dsc.header.w = w;
  s_dsc.header.h = h;
  s_dsc.data_size = uint32_t(w) * h;
  s_dsc.data = s_px;
  lv_img_set_src(s_img, &s_dsc);
  lv_obj_clear_flag(s_img, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
}

// The layout the label on show is drawn with, before the roll narrows it.
static LabelLayout baseLayout() {
  if (s_st->mode == EM_FREE) return labelEditFreeLayout(labelLayoutLoad(LABEL_KIND_SPOOL));
  return labelLayoutLoad(s_st->kind);
}

// The label as it will print, drawn by the renderer like every label.
static void renderPreview() {
  const LabelPrinterConfig c = labelPrinterLoadConfig();
  const LabelLayout base = baseLayout();
  const LabelLayout shown = labelLayoutForMedia(base, c.media_width_mm, c.media_length_mm);
  LabelRaster r{};
  bool ok = false;
  if (s_st->mode == EM_TEMPLATE) {
    ok = s_st->kind == LABEL_KIND_FILAMENT ? labelRenderFilament(c, base, s_st->spool, &r)
                                           : labelRenderSpool(c, base, s_st->spool, &r);
  } else {
    const LabelEdit& e = s_st->mode == EM_FREE ? *s_free : s_st->edit;
    ok = labelRenderEdit(c, base, e, s_st->spool, &r);
  }
  if (ok) showPicture(r, LabelThumbSpec{ labelPrinterContentX(c), r.content_width,
                                         labelLayoutOption(shown, LO_PORTRAIT),
                                         LE_THUMB_W, LE_THUMB_H });
  else showMessage(STR_W_L_PREVIEW_FAIL);
  labelRasterFree(&r);
}

// ---- the rows ----------------------------------------------------------

static void rowTapped(lv_event_t* e) {
  s_tap = (int)(intptr_t)lv_event_get_user_data(e);
  logSDf("BTN: Label edit -> row %d", s_tap);
}

// What a row says on its right (on/off, or the pencil); `dim`: a switch the
// line budget locks.
struct RowNote { const char* text; bool dim; };

// One row: a small caption over its text, or the text alone, and its note.
static void addRow(lv_obj_t* list, int id, const char* caption, const char* text, RowNote rn) {
  const char* note = rn.text;
  const bool dim = rn.dim;
  lv_obj_t* b = lv_btn_create(list);
  lv_obj_set_size(b, LE_ROW_W, LE_ROW_H);
  lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_ROW), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_ROW_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_radius(b, UI_RADIUS_ROW, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(UI_COL_ROW_PRESSED), 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  const bool two = caption && caption[0];
  if (two) {
    lv_obj_t* c = lv_label_create(b);
    lv_label_set_text(c, caption);
    lv_label_set_long_mode(c, LV_LABEL_LONG_DOT);
    lv_obj_set_width(c, LE_TEXT_W);
    lv_obj_set_style_text_font(c, UI_FONT_CAPTION, 0);
    lv_obj_set_style_text_color(c, lv_color_hex(UI_COL_CAPTION), 0);
    lv_obj_set_pos(c, 10, 5);
  }
  const bool empty = !text || !text[0];
  lv_obj_t* t = lv_label_create(b);
  lv_label_set_text(t, empty ? T(STR_LE_EMPTY) : text);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_set_width(t, LE_TEXT_W);
  lv_obj_set_style_text_font(t, UI_FONT_BODY, 0);
  lv_obj_set_style_text_color(t, lv_color_hex(empty || dim ? UI_COL_CAPTION : UI_COL_INK), 0);
  if (two) lv_obj_set_pos(t, 10, 23);
  else lv_obj_align(t, LV_ALIGN_LEFT_MID, 10, 0);
  if (note) {
    lv_obj_t* n = lv_label_create(b);
    lv_label_set_text(n, note);
    lv_obj_set_style_text_font(n, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(n, lv_color_hex(dim ? UI_COL_CAPTION : UI_COL_ACCENT), 0);
    lv_obj_align(n, LV_ALIGN_RIGHT_MID, -10, 0);
  }
  lv_obj_add_event_cb(b, rowTapped, LV_EVENT_CLICKED, (void*)(intptr_t)id);
}

static int partCaption(uint8_t part) {
  return part == LEP_HEAD ? STR_LE_HEAD : part == LEP_BAND ? STR_LE_BAND : STR_LE_TITLE;
}

// The rows of a label: head, band, title, its lines, the code, the reset.
static void editRows(lv_obj_t* list, const LabelEdit& e) {
  int line = 0;
  for (int i = 0; i < e.n; i++) {
    const LabelEditRow& r = e.row[i];
    char caption[32];
    if (r.part == LEP_FACT) snprintf(caption, sizeof(caption), T(STR_LE_LINE), ++line);
    else copyT(caption, sizeof(caption), partCaption(r.part));
    addRow(list, i, caption, r.text, RowNote{ LV_SYMBOL_EDIT, false });
  }
  if (e.code_allowed) {
    addRow(list, LE_ROW_CODE, nullptr, T(STR_LE_CODE), RowNote{ T(e.code ? STR_ON : STR_OFF), false });
    if (e.free)
      addRow(list, LE_ROW_CODE_TEXT, T(STR_LE_CODE_TEXT), e.qr, RowNote{ LV_SYMBOL_EDIT, false });
  }
  addRow(list, LE_ROW_RESET, nullptr, T(e.free ? STR_LE_CLEAR : STR_LE_RESET),
         RowNote{ LV_SYMBOL_REFRESH, false });
}

// The template's switches with the browser's line budget, and its free line.
static void templateRows(lv_obj_t* list) {
  const LabelPrinterConfig c = labelPrinterLoadConfig();
  const LabelLayout l = labelLayoutLoad(s_st->kind);
  const int cap = labelFactCapacity(l, c.media_width_mm, c.media_length_mm);
  const uint32_t kind_fields = labelKindFields(s_st->kind);
  for (int i = 0; i < FIELD_COUNT; i++) {
    const LabelField f = FIELDS[i].field;
    if (!((kind_fields >> f) & 1u)) continue;
    const bool on = labelLayoutHas(l, f);
    const bool locked = (f == LF_QR && !labelPresetHasQr(l.preset)) ||
                        !labelLayoutMayTurnOn(l, f, cap);
    addRow(list, LE_ROW_FIELD + i, nullptr, T(FIELDS[i].caption),
           RowNote{ T(on ? STR_ON : STR_OFF), locked });
  }
  if ((kind_fields >> LF_FREE_TEXT) & 1u)
    addRow(list, LE_ROW_FREE_TEXT, T(STR_W_L_F_TEXT), labelLayoutFreeText(),
           RowNote{ LV_SYMBOL_EDIT, false });
}

// ---- the screen --------------------------------------------------------

static int titleOf(EditMode m) {
  return m == EM_TEMPLATE ? STR_PS_TEMPLATE : m == EM_FREE ? STR_PRN_FREE_LABEL : STR_PS_EDIT_LINES;
}

static void buildPaper() {
  lv_obj_t* paper = lv_obj_create(s_scr);
  lv_obj_set_size(paper, LE_LEFT_W, LE_PAPER_H);
  lv_obj_set_pos(paper, LE_LEFT_X, LE_TOP);
  lv_obj_set_style_bg_color(paper, lv_color_hex(UI_COL_QR_LIGHT), 0);
  lv_obj_set_style_border_width(paper, 0, 0);
  lv_obj_set_style_radius(paper, UI_RADIUS_INPUT, 0);
  lv_obj_set_style_pad_all(paper, LE_PAPER_PAD, 0);
  lv_obj_clear_flag(paper, LV_OBJ_FLAG_SCROLLABLE);
  s_img = lv_img_create(paper);
  lv_obj_set_style_img_recolor(s_img, lv_color_hex(UI_COL_QR_DARK), 0);
  lv_obj_set_style_img_recolor_opa(s_img, LV_OPA_COVER, 0);
  lv_obj_center(s_img);
  s_msg = lv_label_create(paper);
  lv_obj_set_width(s_msg, LE_THUMB_W);
  lv_label_set_long_mode(s_msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(s_msg, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(s_msg, lv_color_hex(UI_COL_QR_DARK), 0);
  lv_obj_set_style_text_font(s_msg, UI_FONT_SMALL, 0);
  lv_obj_center(s_msg);
  lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
}

// Under the paper: the print button, or on the template what it is for.
static void buildUnderPaper() {
  if (s_st->mode == EM_TEMPLATE) {
    lv_obj_t* hint = lv_label_create(s_scr);
    lv_obj_set_width(hint, LE_LEFT_W);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_COL_CAPTION), 0);
    lv_obj_set_style_text_font(hint, UI_FONT_SMALL, 0);
    lv_label_set_text(hint, T(STR_LE_TEMPLATE_HINT));
    lv_obj_set_pos(hint, LE_LEFT_X, LE_BTN_Y + 4);
    return;
  }
  lv_obj_t* go = lv_btn_create(s_scr);
  lv_obj_set_size(go, LE_LEFT_W, LE_BTN_H);
  lv_obj_set_pos(go, LE_LEFT_X, LE_BTN_Y);
  lv_obj_set_style_bg_color(go, lv_color_hex(UI_COL_GO_BG), 0);
  lv_obj_set_style_bg_color(go, lv_color_hex(UI_COL_GO_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(go, UI_RADIUS_ROW, 0);
  lv_obj_set_style_shadow_width(go, 0, 0);
  lv_obj_set_style_border_width(go, 1, 0);
  lv_obj_set_style_border_color(go, lv_color_hex(UI_COL_GO_BG_PRESSED), 0);
  lv_obj_t* l = lv_label_create(go);
  lv_label_set_text(l, T(STR_PS_PRINT));
  lv_obj_set_style_text_font(l, UI_FONT_HEADLINE, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_OK_TEXT_2), 0);
  lv_obj_center(l);
  lv_obj_add_event_cb(go, [](lv_event_t*) {
    logSD("BTN: Label edit -> print");
    s_print = true;
  }, LV_EVENT_CLICKED, NULL);
}

static void build() {
  logSDf("BUILD: Label edit (mode %d, kind %d)", (int)s_st->mode, (int)s_st->kind);
  freePicture();
  releaseScreen(&s_scr);
  s_img = s_msg = nullptr;
  s_scr = buildOverlayScreen();
  buildSubHeader(s_scr, T(titleOf(s_st->mode)), [](lv_event_t*) {
    logSD("BTN: Label edit -> back");
    s_back = true;
  });
  buildPaper();
  buildUnderPaper();
  lv_obj_t* list = lv_obj_create(s_scr);
  lv_obj_set_size(list, LE_LIST_W, 320 - LE_TOP + 2);
  lv_obj_set_pos(list, LE_LIST_X, LE_TOP - 2);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 2, 0);
  lv_obj_set_style_pad_right(list, 10, 0);
  lv_obj_set_style_pad_row(list, 5, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC);
  if (s_st->mode == EM_TEMPLATE) templateRows(list);
  else editRows(list, s_st->mode == EM_FREE ? *s_free : s_st->edit);
  renderPreview();
  lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_HIDDEN);
}

// ---- the keyboard ------------------------------------------------------

static void closeKeyboard() {
  s_ta = nullptr;
  releaseScreen(&s_kb_scr);
}

// The text a row holds now, and the room for it in bytes.
static const char* typingText(size_t* room) {
  *room = LABEL_EDIT_TEXT;
  if (s_typing == LE_ROW_CODE_TEXT) { *room = LABEL_QR_LEN; return s_free ? s_free->qr : ""; }
  if (s_typing == LE_ROW_FREE_TEXT) { *room = LABEL_FREE_TEXT_BYTES; return labelLayoutFreeText(); }
  const LabelEdit* e = s_st->mode == EM_FREE ? s_free : &s_st->edit;
  return e && s_typing >= 0 && s_typing < e->n ? e->row[s_typing].text : "";
}

static void buildKeyboard(int row, const char* title) {
  s_typing = row;
  size_t room = 0;
  const char* now = typingText(&room);
  releaseScreen(&s_kb_scr);
  s_kb_scr = buildOverlayScreen();
  buildSubHeader(s_kb_scr, title, [](lv_event_t*) {
    logSD("BTN: Label keyboard -> back");
    s_kb_leave = true;
  });
  // The limit counts characters, the room bytes: a text of umlauts that runs
  // past it is cut on a character boundary when it is taken (labelEditCut()).
  s_ta = lv_textarea_create(s_kb_scr);
  lv_textarea_set_one_line(s_ta, true);
  lv_textarea_set_max_length(s_ta, room - 1);
  lv_textarea_set_text(s_ta, now);
  lv_obj_set_size(s_ta, 456, 44);
  lv_obj_set_pos(s_ta, 12, LE_TOP + 4);
  lv_obj_set_style_text_font(s_ta, UI_FONT_BODY, 0);
  lv_obj_set_style_text_color(s_ta, lv_color_hex(UI_COL_INK_MAX), 0);
  lv_obj_set_style_bg_color(s_ta, lv_color_hex(UI_COL_ROW_PRESSED), 0);
  lv_obj_set_style_border_color(s_ta, lv_color_hex(UI_COL_POPUP_BORDER), 0);
  lv_obj_add_state(s_ta, LV_STATE_FOCUSED);
  // The layout of the scale's language (ui/key_maps.cpp, Y); its keyboard
  // key reports CANCEL, which leaves the keyboard without a change.
  lv_obj_t* kb = lv_keyboard_create(s_kb_scr);
  keyMapsApply(kb, false);
  lv_keyboard_set_textarea(kb, s_ta);
  lv_obj_set_size(kb, 480, 180);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(kb, lv_color_hex(UI_COL_EMPTY), 0);
  lv_obj_set_style_border_width(kb, 0, 0);
  lv_obj_add_event_cb(kb, [](lv_event_t*) { s_typed = true; }, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(s_ta, [](lv_event_t*) { s_typed = true; }, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(kb, [](lv_event_t*) { s_kb_leave = true; }, LV_EVENT_CANCEL, NULL);
  lv_obj_clear_flag(s_kb_scr, LV_OBJ_FLAG_HIDDEN);
}

// What was typed into the row it was typed for.
static void takeTyped() {
  if (!s_ta) return;
  const char* text = lv_textarea_get_text(s_ta);
  if (s_typing == LE_ROW_CODE_TEXT && s_free) {
    labelEditSetCode(s_free, text);
  } else if (s_typing == LE_ROW_FREE_TEXT) {
    if (!labelLayoutSaveFreeText(text)) logSD("Label edit: free line NOT saved");
  } else {
    LabelEdit* e = s_st->mode == EM_FREE ? s_free : &s_st->edit;
    labelEditSet(e, s_typing, text);
  }
  logSDf("Label edit: row %d typed, %u bytes", s_typing, (unsigned)strlen(text));
}

// ---- opening, the taps, closing ----------------------------------------

static void freeState() {
  if (s_st) heap_caps_free(s_st);
  s_st = nullptr;
}

void labelEditClose() {
  closeKeyboard();
  freePicture();
  releaseScreen(&s_scr);
  s_img = s_msg = nullptr;
  // What a spool's label was changed to is for one print only (Spec V75).
  freeState();
  s_tap = s_typing = -1;
  s_typed = s_kb_leave = s_print = s_back = false;
}

// A spool's label as the template and the roll draw it: its rows (f14).
static bool fillFromSpool() {
  if (!labelSpoolLast(&s_st->spool)) return false;
  const LabelPrinterConfig c = labelPrinterLoadConfig();
  LabelLayout l = labelLayoutForMedia(labelLayoutLoad(s_st->kind), c.media_width_mm,
                                      c.media_length_mm);
  char qr[LABEL_QR_LEN];
  if (s_st->kind == LABEL_KIND_FILAMENT) {
    labelQrForFilament(s_st->spool, qr, sizeof(qr));
    if (!qr[0]) l.fields &= ~(1u << LF_QR);
  } else if (!spoolLinkCopy(s_st->spool.id, qr, sizeof(qr))) {
    labelQrForSpool(s_st->spool.id, qr, sizeof(qr));
  }
  const int cap = labelFactCapacity(l, c.media_width_mm, c.media_length_mm);
  LabelFactList f;
  if (s_st->kind == LABEL_KIND_FILAMENT) labelFilamentFacts(l, s_st->spool, cap, &f);
  else labelSpoolFacts(l, s_st->spool, cap, &f);
  labelEditFromLabel(&s_st->edit, s_st->spool, l, f, qr);
  return true;
}

// A free label from nothing, the rows the roll takes.
static void fillFree() {
  const LabelPrinterConfig c = labelPrinterLoadConfig();
  const LabelLayout l = labelLayoutForMedia(labelEditFreeLayout(labelLayoutLoad(LABEL_KIND_SPOOL)),
                                            c.media_width_mm, c.media_length_mm);
  labelEditFree(s_free, l, labelFactCapacity(l, c.media_width_mm, c.media_length_mm));
}

static void openParked() {
  const EditMode mode = s_want;
  s_want = EM_NONE;
  const LabelPrinterConfig c = labelPrinterLoadConfig();
  // Not for the tape printer, whose label is fixed (label_tape.h).
  if (labelPrinterIsTape(c.model)) {
    logSD("Label edit: not for a tape printer");
    return;
  }
  hideAllOverlays();   // closes this screen too, and frees its state
  s_st = (EditState*)heap_caps_calloc(1, sizeof(EditState), MALLOC_CAP_SPIRAM);
  if (mode == EM_FREE && !s_free)
    s_free = (LabelEdit*)heap_caps_calloc(1, sizeof(LabelEdit), MALLOC_CAP_SPIRAM);
  if (!s_st || (mode == EM_FREE && !s_free)) {
    logSD("Label edit: no PSRAM, not opened");
    freeState();
    return;
  }
  s_st->mode = mode;
  s_st->kind = s_want_kind;
  if (mode == EM_FREE) {
    if (!s_free->n) fillFree();   // kept from the last time until a restart
  } else if (!labelSpoolLast(&s_st->spool) || (mode == EM_SPOOL && !fillFromSpool())) {
    logSD("Label edit: no spool, not opened");
    freeState();
    show_print_sheet_pending = true;
    return;
  }
  logSDf("SHOW: Label edit, %s", mode == EM_FREE ? "free label"
                                  : mode == EM_TEMPLATE ? "template" : "spool label");
  build();
}

// A template switch: on or off within the line budget, saved at once.
static void flipField(int i) {
  const LabelPrinterConfig c = labelPrinterLoadConfig();
  LabelLayout l = labelLayoutLoad(s_st->kind);
  const LabelField f = FIELDS[i].field;
  const int cap = labelFactCapacity(l, c.media_width_mm, c.media_length_mm);
  if (f == LF_QR && !labelPresetHasQr(l.preset)) return;
  if (!labelLayoutMayTurnOn(l, f, cap)) {
    logSDf("Label edit: field %d stays off, %d of %d lines on", (int)f, labelLayoutFactCount(l), cap);
    return;
  }
  l.fields ^= 1u << f;
  if (!labelLayoutSave(l, s_st->kind)) logSD("Label edit: template NOT saved");
}

static void tapped(int id) {
  LabelEdit* e = s_st->mode == EM_FREE ? s_free : &s_st->edit;
  if (id >= LE_ROW_FIELD && id < LE_ROW_FIELD + FIELD_COUNT) { flipField(id - LE_ROW_FIELD); build(); return; }
  if (id == LE_ROW_FREE_TEXT) { buildKeyboard(id, T(STR_W_L_F_TEXT)); return; }
  if (id == LE_ROW_CODE_TEXT) { buildKeyboard(id, T(STR_LE_CODE_TEXT)); return; }
  if (id == LE_ROW_CODE) {
    // A free label without a text for its code asks for one first.
    if (e->free && !e->qr[0]) { buildKeyboard(LE_ROW_CODE_TEXT, T(STR_LE_CODE_TEXT)); return; }
    e->code = !e->code;
    build();
    return;
  }
  if (id == LE_ROW_RESET) {
    if (e->free) fillFree();
    else fillFromSpool();
    build();
    return;
  }
  if (id >= 0 && id < e->n) {
    char title[32];
    if (e->row[id].part == LEP_FACT) {
      int line = 0;
      for (int i = 0; i <= id; i++) line += e->row[i].part == LEP_FACT;
      snprintf(title, sizeof(title), T(STR_LE_LINE), line);
    } else {
      copyT(title, sizeof(title), partCaption(e->row[id].part));
    }
    buildKeyboard(id, title);
  }
}

// What a spool's label was changed to goes to the printer once and is then
// forgotten; a free label stays on screen for the next one.
static void printShown() {
  if (s_st->mode == EM_FREE) {
    printerPrintEdit(*s_free, s_st->spool, LABEL_KIND_SPOOL);
    return;
  }
  EditState* st = s_st;
  s_st = nullptr;               // the close below must not free it
  labelEditClose();
  printerPrintEdit(st->edit, st->spool, st->kind);
  heap_caps_free(st);
}

void labelEditTick() {
  if (s_want != EM_NONE) openParked();
  if (!s_st) return;
  if (s_typed) {
    s_typed = false;
    takeTyped();
    closeKeyboard();
    build();
  }
  if (s_kb_leave) {
    s_kb_leave = false;
    closeKeyboard();
  }
  if (s_tap >= 0) {
    const int id = s_tap;
    s_tap = -1;
    tapped(id);
  }
  if (s_print) {
    s_print = false;
    printShown();
    return;
  }
  if (s_back) {
    const EditMode mode = s_st->mode;
    labelEditClose();
    if (mode == EM_FREE) show_printer_pending = true;
    else show_print_sheet_pending = true;
  }
}
