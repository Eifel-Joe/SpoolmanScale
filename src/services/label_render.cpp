#include "services/label_render.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/backend.h"
#include "services/label_facts.h"
#include "services/text_util.h"
#include "src/extra/libs/qrcode/qrcodegen.h"

// The fonts, from src/fonts/: the large ones are compiled in by their own
// guard and were unused until now. Only the sizes up to 24 have the French
// supplement behind them; a line the big ones cannot spell drops to 24.
// 30 to 48 are what a 300 dpi head prints the others in (FONTS_300 below).
LV_FONT_DECLARE(lv_font_montserrat_ext_48);
LV_FONT_DECLARE(lv_font_montserrat_ext_40);
LV_FONT_DECLARE(lv_font_montserrat_ext_36);
LV_FONT_DECLARE(lv_font_montserrat_ext_32);
LV_FONT_DECLARE(lv_font_montserrat_ext_30);
LV_FONT_DECLARE(lv_font_montserrat_ext_28);
LV_FONT_DECLARE(lv_font_montserrat_ext_24);
LV_FONT_DECLARE(lv_font_montserrat_ext_22);
LV_FONT_DECLARE(lv_font_montserrat_ext_20);
LV_FONT_DECLARE(lv_font_montserrat_ext_16);
LV_FONT_DECLARE(lv_font_montserrat_ext_14);

// The label's geometry in dots at 203 dpi, 8 dots to the millimetre, and its
// fonts at that resolution. A finer head prints the same millimetres: every
// length goes through dots() and every font through sized() on its way to the
// canvas, and at 203 dpi both hand back what they were given.
#define LR_BASE_DPI      203
#define LR_MARGIN_PX       8    // 1 mm: the stock is never cut exactly
#define LR_GAP_PX          4    // between the blocks
#define LR_COLUMN_GAP_PX  12    // between two columns of facts
// The blocks grow with the roll. 40 x 30 is the base; from LR_TALL_PX in
// both directions the band, the fonts and the fact lines take the larger sizes.
#define LR_TALL_PX       300
#define LR_BAND_H_PX      30    // the material band on the base size
#define LR_BAND_H_TALL_PX 40
#define LR_BAND_H_BIG_PX  60    // the material-first band, a quarter of 30 mm
#define LR_FACT_LINE_PX   22    // one fact line in the 16 px font
#define LR_FACT_LINE_TALL_PX 28 // one in the 20 px font
#define LR_FACT_LINE_SMALL_PX 19 // one in the 14 px font, next to a big code
#define LR_FACTS_MIN_W   130    // the facts keep this much next to a code
#define LR_QR_MAX_MM      25    // 200 dots at 203 dpi: larger reads no better
#define LR_QR_SMALL_MM    13    // the material-first code: a spool URL at 3 dots
#define LR_QR_MIN_PX      64
// Extra stroke width of the small lines and the band. The large ones, the
// maker and the title, print without it: with it everything read a touch
// heavy, without it the small lines came out thin (Nikolai, 25.09.2026).
#define LR_BOLD_DOTS       1
#define LR_WRAP_ROWS       4    // what a wrapped paragraph is sized for
// A thermal head bleeds each dot a little, so a module under 3 dots (0.375 mm)
// fills in. And a reader needs white around the code: the standard asks for
// 4 modules, 2 is what most phones still take. The first test labels had
// neither - the code sat next to the text with whatever the rounding left -
// and did not read (25.09.2026).
#define LR_QR_MODULE_MIN   3
#define LR_QR_QUIET        4
#define LR_QR_QUIET_TIGHT  2
#define LR_QR_MAX_VERSION 10    // 57 modules: far more than a spool URL needs
#define LR_QR_GOOD_PX     96    // under this, smaller fact lines buy code size
#define LR_BLACK_BELOW   128    // brightness under which a canvas pixel prints
// The calibration page's ruler, in dots from the top of the label.
#define LC_NUM_Y          12    // the numbers' row, and the top of their ticks
#define LC_NUM_EVERY       4    // a number every 4 mm
#define LC_NUM_GAP         2    // from a tick to its number
#define LC_NUM_W          28    // "-12" in 16 px: the rest of the 4 mm to the next
#define LC_NUM_EXTRA       3    // numbers past where the edge can be, each side
#define LC_BASE_Y         44    // the ruler's line; the ticks stand on it
#define LC_BASE_PX         2
#define LC_TICK_PX         2
#define LC_TICK_MM         6    // every millimetre
#define LC_TICK_2MM       12    // every second one
#define LC_BOX_GAP        12    // from the ruler to the frame
#define LC_FRAME_PX        3
#define LC_TEXT_GAP       14    // from the frame's top to the first line

// The resolution of the label being drawn. Each entry point sets it before it
// draws; they all run on the loop task, one label at a time.
static uint16_t s_dpi = LR_BASE_DPI;
// Whether the canvas is the label upright: as wide as the roll is long, and
// turned a quarter left onto the raster, its top at the row's left end.
static bool s_turned = false;

// A length of the geometry above in dots of the head that prints it, rounded.
static lv_coord_t dots(lv_coord_t base) {
  return (lv_coord_t)((int32_t(base) * s_dpi + LR_BASE_DPI / 2) / LR_BASE_DPI);
}

// The fonts on a finer head: the 203 dpi size times 1.48, as near as the even
// sizes in src/fonts/ go. The 36 would want 53; 48 is the largest there is.
struct FontStep { const lv_font_t* base; const lv_font_t* fine; };
static const FontStep FONTS_300[] = {
  { &lv_font_montserrat_ext_14, &lv_font_montserrat_ext_20 },
  { &lv_font_montserrat_ext_16, &lv_font_montserrat_ext_24 },
  { &lv_font_montserrat_ext_20, &lv_font_montserrat_ext_30 },
  { &lv_font_montserrat_ext_22, &lv_font_montserrat_ext_32 },
  { &lv_font_montserrat_ext_24, &lv_font_montserrat_ext_36 },
  { &lv_font_montserrat_ext_28, &lv_font_montserrat_ext_40 },
  { &lv_font_montserrat_ext_36, &lv_font_montserrat_ext_48 },
};

// The font a 203 dpi font prints in on this head. The layout chooses among
// the 203 dpi fonts and compares them; only measuring and drawing take this.
static const lv_font_t* sized(const lv_font_t* base) {
  if (s_dpi <= LR_BASE_DPI) return base;
  for (const FontStep& s : FONTS_300)
    if (s.base == base) return s.fine;
  return base;
}

static lv_coord_t lineHeight(const lv_font_t* base) { return sized(base)->line_height; }

void labelRasterFree(LabelRaster* image) {
  if (!image) return;
  free(image->pixels);
  *image = LabelRaster{};
}

// The canvas into the raster: black is anything darker than mid grey, and the
// canvas starts at dot x0 of the print row, where the stock runs.
static bool packCanvas(lv_obj_t* canvas, uint16_t content_w, uint16_t h,
                       uint16_t row_w, uint16_t x0, LabelRaster* out) {
  const uint16_t row_bytes = (row_w + 7) / 8;
  const size_t length = size_t(row_bytes) * h;
  uint8_t* px = (uint8_t*)heap_caps_calloc(length, 1, MALLOC_CAP_SPIRAM);
  if (!px) { logSD("Label: no PSRAM for the raster"); return false; }
  for (uint16_t y = 0; y < h; y++) {
    uint8_t* row = px + size_t(y) * row_bytes;
    for (uint16_t x = 0; x < content_w; x++) {
      // Upright, raster dot (x, y) is canvas dot (h - 1 - y, x).
      const lv_color_t c = s_turned ? lv_canvas_get_px(canvas, h - 1 - y, x)
                                    : lv_canvas_get_px(canvas, x, y);
      if (lv_color_brightness(c) < LR_BLACK_BELOW) {
        const uint16_t rx = x0 + x;
        row[rx / 8] |= 0x80 >> (rx % 8);
      }
    }
  }
  out->width = row_w;
  out->height = h;
  out->row_bytes = row_bytes;
  out->pixels = px;
  out->length = length;
  out->content_width = content_w;
  return labelRasterValid(*out);
}

// Whether the large fonts can spell a line: ASCII and the German letters they
// were generated with. Anything else has a glyph only in the sizes up to 24.
static bool bigFontCovers(const char* s) {
  for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
    if (*p < 0x80) continue;
    if (*p == 0xC3 && p[1]) {
      const unsigned char n = p[1];
      if (n == 0x84 || n == 0x96 || n == 0x9C || n == 0x9F || n == 0xA4 || n == 0xB6 || n == 0xBC) { p++; continue; }
    }
    return false;
  }
  return true;
}

// Whether a font, in the size this head prints it, can spell a line: one with
// the French supplement behind it can spell anything the labels carry.
static bool canSpell(const lv_font_t* base, const char* s) {
  return sized(base)->fallback != nullptr || bigFontCovers(s);
}

static lv_coord_t boldFor(const lv_font_t* font) {
  return font->line_height > lv_font_montserrat_ext_24.line_height ? 0 : dots(LR_BOLD_DOTS);
}

static lv_coord_t textWidth(const char* s, const lv_font_t* font) {
  return lv_txt_get_width(s, strlen(s), sized(font), 0, LV_TEXT_FLAG_NONE);
}

// The largest of the fonts that fits the line into the width, the last one
// when none does; the line is then cut to it.
static const lv_font_t* fitFont(const char* text, lv_coord_t w,
                                const lv_font_t* const* fonts, int count) {
  for (int i = 0; i < count; i++) {
    if (!canSpell(fonts[i], text)) continue;
    if (textWidth(text, fonts[i]) <= w) return fonts[i];
  }
  return fonts[count - 1];
}

// One line, cut to what the font really fits into the width, on a character
// boundary. Cut, never wrapped: a wrapped line would run into the next block.
static void drawLine(lv_obj_t* canvas, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                     const lv_font_t* font, lv_color_t color, lv_text_align_t align,
                     const char* text) {
  if (!text || !text[0]) return;
  const lv_coord_t bold = boldFor(font);
  char line[LABEL_LINE_LEN * 2 + 4];
  size_t budget = strlen(text);
  for (;;) {
    utf8Cut(text, budget, line, sizeof(line));
    const lv_coord_t used = textWidth(line, font) + bold;
    if (used <= w || budget == 0) break;
    const size_t next = (size_t)((uint32_t)budget * w / used);
    budget = next < budget ? next : budget - 1;
  }
  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.color = color;
  dsc.font = sized(font);
  dsc.align = align;
  // Drawn once more a dot to the right: every stroke one dot wider. The
  // thermal head printed Montserrat's regular weight thin, and white text on
  // the material band came out grey because the black around it bled into
  // its strokes (Nikolai, 25.09.2026). The same pass widens those too.
  for (lv_coord_t dx = 0; dx <= bold; dx++)
    lv_canvas_draw_text(canvas, x + dx, y, w - bold, &dsc, line);
}

// A paragraph, wrapped by LVGL into the width. Returns its height.
static lv_coord_t paragraphHeight(const char* text, const lv_font_t* font, lv_coord_t w) {
  lv_point_t size;
  lv_txt_get_size(&size, text, sized(font), 0, 0, w - boldFor(font), LV_TEXT_FLAG_NONE);
  return size.y;
}

static void drawParagraph(lv_obj_t* canvas, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                          const lv_font_t* font, const char* text) {
  const lv_coord_t bold = boldFor(font);
  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.color = lv_color_black();
  dsc.font = sized(font);
  for (lv_coord_t dx = 0; dx <= bold; dx++)
    lv_canvas_draw_text(canvas, x + dx, y, w - bold, &dsc, text);
}

static void fillRect(lv_obj_t* canvas, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                     lv_coord_t h, lv_color_t color) {
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_color = color;
  dsc.bg_opa = LV_OPA_COVER;
  dsc.border_width = 0;
  dsc.radius = 0;
  lv_canvas_draw_rect(canvas, x, y, w, h, &dsc);
}

// The code, drawn module by module into a box of box x box dots at x, y:
// whole dots per module, the quiet zone inside the box and white, the rest
// centred. Returns the edge length used, 0 when it does not fit readably.
// With no canvas it only measures; `silent` keeps a measurement that tries
// several boxes out of the log, the drawing call logs what it had to drop.
static lv_coord_t drawQr(lv_obj_t* canvas, lv_coord_t x, lv_coord_t y,
                         lv_coord_t box, const char* text, bool silent = false) {
  uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(LR_QR_MAX_VERSION)];
  uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(LR_QR_MAX_VERSION)];
  if (!qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_MEDIUM, 1,
                            LR_QR_MAX_VERSION, qrcodegen_Mask_AUTO, true)) {
    if (!silent) logSDf("Label: QR does not fit version %d: %s", LR_QR_MAX_VERSION, text);
    return 0;
  }
  const int size = qrcodegen_getSize(qr);
  int quiet = LR_QR_QUIET;
  // At 300 dpi 3 dots would be a module of 0.25 mm: the minimum is a length.
  const int module_min = dots(LR_QR_MODULE_MIN);
  int scale = box / (size + 2 * quiet);
  if (scale < module_min) {
    quiet = LR_QR_QUIET_TIGHT;
    scale = box / (size + 2 * quiet);
  }
  if (scale < module_min) {
    if (!silent) logSDf("Label: QR of %d modules does not fit %d dots", size, (int)box);
    return 0;
  }
  const lv_coord_t total = (size + 2 * quiet) * scale;
  if (!canvas) return total;
  const lv_coord_t ox = x + (box - total) / 2;
  const lv_coord_t oy = y + (box - total) / 2;
  fillRect(canvas, ox, oy, total, total, lv_color_white());
  const lv_coord_t mx = ox + quiet * scale;
  const lv_coord_t my = oy + quiet * scale;
  for (int r = 0; r < size; r++) {
    // A run of dark modules is one rectangle rather than one each.
    for (int c = 0; c < size; ) {
      if (!qrcodegen_getModule(qr, c, r)) { c++; continue; }
      int run = 1;
      while (c + run < size && qrcodegen_getModule(qr, c + run, r)) run++;
      fillRect(canvas, mx + c * scale, my + r * scale, run * scale, scale, lv_color_black());
      c += run;
    }
  }
  logSDf("Label: QR %d modules, %d dots each, quiet %d, %d dots", size, scale, quiet, (int)total);
  return total;
}

// What one label carries besides its layout: the header from `d`, under it
// `lines` in small type next to the code, or, with `wrap`, lines[0] as one
// paragraph wrapped into that column. No code without `qr`. `what` names the
// label in the log. With `facts`, the same n lines as caption and value, so
// the body can set them in two lines where the room allows.
struct LabelContent {
  const SpoolLabelData* d;
  const char* const* lines;
  int n;
  bool wrap;
  const char* qr;
  const char* what;
  const LabelFact* facts;
};

// The part of the canvas the blocks are laid into, in dots, and how the code
// shares it: its largest edge, and whether the text next to it keeps the
// width of its widest line. The standard arrangement gives the code all it
// can (and a long date line a cut); the others keep the text whole.
struct LabelArea {
  lv_coord_t x, y, w, bottom;
  lv_coord_t qr_max;
  bool keep_text;
  const lv_font_t* facts_font;   // null: by the label's size
  bool columns;                  // the facts may stand in two columns
};

// One of two columns side by side.
static lv_coord_t columnWidth(const LabelArea& a) { return (a.w - dots(LR_COLUMN_GAP_PX)) / 2; }

// Where the body put the code and how it set the facts, for the log.
struct LabelBody { lv_coord_t qr; bool beside; const char* facts; };

// The header, each block where the layout keeps it: the maker as large as
// the width carries, the material white on a black band, the filament's name
// as the title. Returns the y under it.
static lv_coord_t drawHeader(lv_obj_t* canvas, const LabelArea& a, bool tall,
                             const SpoolLabelData& d, const LabelLayout& layout) {
  const lv_color_t black = lv_color_black();
  lv_coord_t y = a.y;
  // The compact arrangement puts the maker among the facts instead.
  if (d.vendor[0] && labelLayoutHas(layout, LF_VENDOR) &&
      layout.preset != LABEL_PRESET_COMPACT) {
    static const lv_font_t* const base[] = { &lv_font_montserrat_ext_36, &lv_font_montserrat_ext_28, &lv_font_montserrat_ext_24 };
    // No 44 px on the larger sizes: that one line cost 85 KB of flash.
    const lv_font_t* f = fitFont(d.vendor, a.w, base, 3);
    drawLine(canvas, a.x, y, a.w, f, black, LV_TEXT_ALIGN_CENTER, d.vendor);
    y += lineHeight(f) + dots(LR_GAP_PX);
  }
  if (d.material[0] && labelLayoutHas(layout, LF_MATERIAL)) {
    const lv_coord_t band_h = dots(tall ? LR_BAND_H_TALL_PX : LR_BAND_H_PX);
    // Plain: the same line in black on white, for a roll that smears a band.
    const bool plain = labelLayoutOption(layout, LO_MATERIAL_PLAIN);
    if (!plain) fillRect(canvas, a.x, y, a.w, band_h, black);
    const lv_font_t* f = tall ? &lv_font_montserrat_ext_28 : &lv_font_montserrat_ext_22;
    drawLine(canvas, a.x, y + (band_h - lineHeight(f)) / 2, a.w, f,
             plain ? black : lv_color_white(), LV_TEXT_ALIGN_CENTER, d.material);
    y += band_h + dots(LR_GAP_PX);
  }
  if (d.name[0] && labelLayoutHas(layout, LF_NAME)) {
    static const lv_font_t* const base[] = { &lv_font_montserrat_ext_28, &lv_font_montserrat_ext_24, &lv_font_montserrat_ext_20 };
    static const lv_font_t* const big[]  = { &lv_font_montserrat_ext_36, &lv_font_montserrat_ext_28, &lv_font_montserrat_ext_24, &lv_font_montserrat_ext_20 };
    const lv_font_t* f = tall ? fitFont(d.name, a.w, big, 4) : fitFont(d.name, a.w, base, 3);
    drawLine(canvas, a.x, y, a.w, f, black, LV_TEXT_ALIGN_CENTER, d.name);
    y += lineHeight(f) + dots(LR_GAP_PX);
  }
  return y;
}

// The material-first header: the material on a band across a quarter of the
// label, as large as it fits, the maker and the filament's name in one size
// under it. Returns the y under it.
static lv_coord_t drawMaterialHeader(lv_obj_t* canvas, const LabelArea& a,
                                     const SpoolLabelData& d, const LabelLayout& layout) {
  const lv_color_t black = lv_color_black();
  const lv_coord_t gap = dots(LR_GAP_PX);
  lv_coord_t y = a.y;
  if (d.material[0] && labelLayoutHas(layout, LF_MATERIAL)) {
    static const lv_font_t* const big[] = { &lv_font_montserrat_ext_36, &lv_font_montserrat_ext_28, &lv_font_montserrat_ext_24 };
    const lv_coord_t band_h = dots(LR_BAND_H_BIG_PX);
    const bool plain = labelLayoutOption(layout, LO_MATERIAL_PLAIN);
    if (!plain) fillRect(canvas, a.x, y, a.w, band_h, black);
    const lv_font_t* f = fitFont(d.material, a.w - 2 * gap, big, 3);
    drawLine(canvas, a.x, y + (band_h - lineHeight(f)) / 2, a.w, f,
             plain ? black : lv_color_white(), LV_TEXT_ALIGN_CENTER, d.material);
    y += band_h + gap;
  }
  static const lv_font_t* const line[] = { &lv_font_montserrat_ext_20, &lv_font_montserrat_ext_16 };
  const char* const under[] = { labelLayoutHas(layout, LF_VENDOR) ? d.vendor : "",
                                labelLayoutHas(layout, LF_NAME) ? d.name : "" };
  for (const char* text : under) {
    if (!text[0]) continue;
    const lv_font_t* f = fitFont(text, a.w, line, 2);
    drawLine(canvas, a.x, y, a.w, f, black, LV_TEXT_ALIGN_CENTER, text);
    y += lineHeight(f) + gap;
  }
  return y;
}

// The width the text keeps next to the code: the fixed minimum, or, where the
// arrangement keeps the text whole, its widest line, as long as the code
// still comes out at a good size.
static lv_coord_t textKeep(const LabelArea& a, const LabelContent& c, const lv_font_t* f) {
  const lv_coord_t least = dots(LR_FACTS_MIN_W);
  if (!a.keep_text || c.wrap) return least;
  lv_coord_t widest = least;
  for (int i = 0; i < c.n; i++) {
    const lv_coord_t w = textWidth(c.lines[i], f) + boldFor(f);
    if (w > widest) widest = w;
  }
  const lv_coord_t most = a.w - dots(LR_QR_GOOD_PX) - dots(LR_GAP_PX);
  return widest > most ? (most > least ? most : least) : widest;
}

// How the body sets the facts in one line each: their font and line, where
// the code goes and how large it comes out (0: no code).
struct BodyPlan {
  const lv_font_t* font;
  lv_coord_t line;
  lv_coord_t qr;      // the code's box
  lv_coord_t code;    // the code as drawn, whole dots per module
  bool beside;
};

// The facts and the code under the header. The code goes where it comes out
// larger: next to the facts on a wide label, under them on a tall one, and it
// takes all the height it gets. When that leaves it small, the fact lines
// step down a size first.
static BodyPlan planOneLine(const LabelArea& a, bool tall, const LabelContent& c) {
  const lv_coord_t avail_h = a.bottom - a.y;
  const int rows = c.wrap ? LR_WRAP_ROWS : c.n;
  BodyPlan p{ tall ? &lv_font_montserrat_ext_20 : &lv_font_montserrat_ext_16,
              dots(tall ? LR_FACT_LINE_TALL_PX : LR_FACT_LINE_PX), 0, 0, true };
  if (a.facts_font) { p.font = a.facts_font; p.line = dots(LR_FACT_LINE_SMALL_PX); }
  for (int pass = 0; pass < 2; pass++) {
    lv_coord_t qr_beside = a.w - textKeep(a, c, p.font) - dots(LR_GAP_PX);
    if (qr_beside > avail_h) qr_beside = avail_h;
    lv_coord_t qr_below = avail_h - rows * p.line - dots(LR_GAP_PX);
    if (qr_below > a.w) qr_below = a.w;
    p.beside = qr_beside >= qr_below;
    p.qr = p.beside ? qr_beside : qr_below;
    if (p.qr > a.qr_max) p.qr = a.qr_max;
    if (p.qr >= dots(LR_QR_GOOD_PX) || p.font != &lv_font_montserrat_ext_20) break;
    p.font = &lv_font_montserrat_ext_16;
    p.line = dots(LR_FACT_LINE_PX);
  }
  // The code comes out in whole dots per module and is mostly smaller than its
  // box: the text column reaches to the code, not to the box, which gave a
  // date line on 40 x 30 the room it was cut short of.
  if (p.qr >= dots(LR_QR_MIN_PX) && c.qr && c.qr[0]) p.code = drawQr(nullptr, 0, 0, p.qr, c.qr);
  return p;
}

static void drawOneLine(lv_obj_t* canvas, const LabelArea& a, const LabelContent& c,
                        const BodyPlan& p) {
  const lv_coord_t facts_top = a.y;
  const lv_coord_t avail_h = a.bottom - facts_top;
  const int rows = c.wrap ? LR_WRAP_ROWS : c.n;
  const lv_font_t* fact_font = p.font;
  const lv_coord_t facts_w = (p.beside && p.code) ? a.w - p.code - dots(LR_GAP_PX) : a.w;
  lv_coord_t text_h = rows * p.line;
  if (c.wrap && c.n > 0) {
    // One size down when the paragraph does not fit the height next to the code.
    text_h = paragraphHeight(c.lines[0], fact_font, facts_w);
    if (text_h > avail_h) {
      fact_font = &lv_font_montserrat_ext_14;
      text_h = paragraphHeight(c.lines[0], fact_font, facts_w);
    }
    const lv_coord_t py = p.beside && text_h < avail_h ? facts_top + (avail_h - text_h) / 2 : facts_top;
    drawParagraph(canvas, a.x, py, facts_w, fact_font, c.lines[0]);
  } else {
    // Next to the code the lines stand in the middle of the height they share.
    const lv_coord_t block = c.n * p.line;
    const lv_coord_t fy = p.beside && block < avail_h ? facts_top + (avail_h - block) / 2 : facts_top;
    for (int i = 0; i < c.n; i++)
      drawLine(canvas, a.x, fy + i * p.line, facts_w, fact_font,
               lv_color_black(), LV_TEXT_ALIGN_LEFT, c.lines[i]);
  }
  if (p.code) {
    const lv_coord_t qx = p.beside ? a.x + a.w - p.code : a.x + (a.w - p.code) / 2;
    const lv_coord_t qy = p.beside ? facts_top + (avail_h - p.code) / 2 : facts_top + text_h + dots(LR_GAP_PX);
    drawQr(canvas, qx, qy, p.code, c.qr);
  }
}

// The larger sizes for the facts, tried in this order: two lines each (the
// caption small, the value large under it), then one line in a larger font.
// A fact with no value, the project name, stays in the small font. The
// largest is for the tall sizes only; on the base size no fact outgrows the
// filament's name.
struct FactStyle { const lv_font_t* value; const lv_font_t* caption; bool two_lines; };
static const FactStyle FACT_STYLES[] = {
  { &lv_font_montserrat_ext_28, &lv_font_montserrat_ext_16, true },
  { &lv_font_montserrat_ext_24, &lv_font_montserrat_ext_14, true },
  { &lv_font_montserrat_ext_20, &lv_font_montserrat_ext_14, true },
  { &lv_font_montserrat_ext_24, &lv_font_montserrat_ext_14, false },
  { &lv_font_montserrat_ext_20, &lv_font_montserrat_ext_14, false },
  // The small lines once more, for a label without a code only: in two
  // columns they hold what one column would push off the label.
  { &lv_font_montserrat_ext_16, &lv_font_montserrat_ext_14, false },
};
static const int FACT_STYLE_COUNT = sizeof(FACT_STYLES) / sizeof(FACT_STYLES[0]);
// The last one, which only a label without a code may take.
static const FactStyle* const FACT_STYLE_NO_CODE = &FACT_STYLES[FACT_STYLE_COUNT - 1];

// cols: 2 when the facts stand in two columns, the first half on the left.
struct FactsPlan { const FactStyle* style; lv_coord_t code; lv_coord_t text_h; int cols; };

static bool hasText(const char* s) { return s && s[0]; }

// What fact i draws in the value font: its value, or its whole line.
static const char* largeText(const LabelContent& c, int i, const FactStyle& s) {
  if (!hasText(c.facts[i].value)) return nullptr;
  return s.two_lines ? c.facts[i].value : c.lines[i];
}

// What fact i draws in the caption font.
static const char* smallText(const LabelContent& c, int i, const FactStyle& s) {
  const LabelFact& f = c.facts[i];
  if (!hasText(f.value)) return f.caption;
  return s.two_lines && hasText(f.caption) ? f.caption : nullptr;
}

// The height of facts from..to-1 stacked.
static lv_coord_t factsHeight(const LabelContent& c, const FactStyle& s, int from, int to) {
  lv_coord_t h = 0;
  for (int i = from; i < to; i++) {
    if (i > from) h += dots(LR_GAP_PX);
    if (smallText(c, i, s)) h += lineHeight(s.caption);
    if (largeText(c, i, s)) h += lineHeight(s.value);
  }
  return h;
}

// The widest of the lines; 0 when one has a letter the font cannot spell.
static lv_coord_t factsWidth(const LabelContent& c, const FactStyle& s) {
  lv_coord_t widest = 0;
  for (int i = 0; i < c.n; i++) {
    const char* small = smallText(c, i, s);
    const char* large = largeText(c, i, s);
    if (small) widest = LV_MAX(widest, textWidth(small, s.caption) + boldFor(s.caption));
    if (!large) continue;
    if (!canSpell(s.value, large)) return 0;
    widest = LV_MAX(widest, textWidth(large, s.value) + boldFor(s.value));
  }
  return widest;
}

// The facts larger, when the label has the room: the first style whose lines
// fit the height, whose lines fit the width whole, and that leaves the code
// no smaller than the small lines did. So a label with few fields reads
// larger, and one with all of them prints as before.
static bool planLarger(const LabelArea& a, bool tall, const LabelContent& c,
                       const BodyPlan& one, FactsPlan* out) {
  if (c.wrap || !c.facts || c.n == 0) return false;
  const lv_coord_t avail_h = a.bottom - a.y;
  for (int i = 0; i < FACT_STYLE_COUNT; i++) {
    const FactStyle& s = FACT_STYLES[i];
    if (&s == FACT_STYLE_NO_CODE ? !a.columns : s.value->line_height <= one.font->line_height)
      continue;
    if (!tall && s.value->line_height > lv_font_montserrat_ext_24.line_height) continue;
    const lv_coord_t w = factsWidth(c, s);
    if (w == 0) continue;
    const lv_coord_t h = factsHeight(c, s, 0, c.n);
    if (w > a.w || h > avail_h) {
      // Two columns where the arrangement has no code to share the width with.
      const int half = (c.n + 1) / 2;
      const lv_coord_t h2 = LV_MAX(factsHeight(c, s, 0, half), factsHeight(c, s, half, c.n));
      if (!a.columns || one.code || c.n < 2 || w > columnWidth(a) || h2 > avail_h) continue;
      *out = FactsPlan{ &s, 0, h2, 2 };
      return true;
    }
    lv_coord_t code = 0;
    if (one.code) {
      lv_coord_t box = one.beside ? a.w - w - dots(LR_GAP_PX) : avail_h - h - dots(LR_GAP_PX);
      box = LV_MIN(box, one.beside ? avail_h : a.w);
      box = LV_MIN(box, a.qr_max);
      code = box >= dots(LR_QR_MIN_PX) ? drawQr(nullptr, 0, 0, box, c.qr, true) : 0;
      if (code < one.code) continue;
    }
    *out = FactsPlan{ &s, code, h, 1 };
    return true;
  }
  return false;
}

// Facts from..to-1 one under the other from y, each cut to the width.
static void drawFactColumn(lv_obj_t* canvas, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                           const LabelContent& c, const FactStyle& s, int from, int to) {
  for (int i = from; i < to; i++) {
    if (i > from) y += dots(LR_GAP_PX);
    if (const char* small = smallText(c, i, s)) {
      drawLine(canvas, x, y, w, s.caption, lv_color_black(), LV_TEXT_ALIGN_LEFT, small);
      y += lineHeight(s.caption);
    }
    if (const char* large = largeText(c, i, s)) {
      drawLine(canvas, x, y, w, s.value, lv_color_black(), LV_TEXT_ALIGN_LEFT, large);
      y += lineHeight(s.value);
    }
  }
}

static void drawLarger(lv_obj_t* canvas, const LabelArea& a, const LabelContent& c,
                       const FactsPlan& p, bool beside) {
  const lv_coord_t avail_h = a.bottom - a.y;
  const lv_coord_t text_w = (beside && p.code) ? a.w - p.code - dots(LR_GAP_PX) : a.w;
  const lv_coord_t y = beside && p.text_h < avail_h ? a.y + (avail_h - p.text_h) / 2 : a.y;
  if (p.cols == 2) {
    const int half = (c.n + 1) / 2;
    const lv_coord_t col_w = columnWidth(a);
    drawFactColumn(canvas, a.x, y, col_w, c, *p.style, 0, half);
    drawFactColumn(canvas, a.x + a.w - col_w, y, col_w, c, *p.style, half, c.n);
  } else {
    drawFactColumn(canvas, a.x, y, text_w, c, *p.style, 0, c.n);
  }
  if (p.code) {
    const lv_coord_t qx = beside ? a.x + a.w - p.code : a.x + (a.w - p.code) / 2;
    const lv_coord_t qy = beside ? a.y + (avail_h - p.code) / 2 : a.y + p.text_h + dots(LR_GAP_PX);
    drawQr(canvas, qx, qy, p.code, c.qr);
  }
}

static LabelBody drawBody(lv_obj_t* canvas, const LabelArea& a, bool tall,
                          const LabelContent& c) {
  const BodyPlan one = planOneLine(a, tall, c);
  FactsPlan larger;
  if (planLarger(a, tall, c, one, &larger)) {
    drawLarger(canvas, a, c, larger, one.beside);
    return LabelBody{ larger.code, one.beside, larger.style->two_lines ? "two lines" : "large lines" };
  }
  drawOneLine(canvas, a, c, one);
  return LabelBody{ one.code ? one.qr : (lv_coord_t)0, one.beside, "small lines" };
}

// The big-code arrangement: the code at the right edge, as tall as the label
// allows while the text keeps its column. Narrows the area to that column
// and returns the code's edge, 0 when there is no code to draw.
static lv_coord_t drawSideQr(lv_obj_t* canvas, LabelArea* a, const LabelContent& c) {
  if (!c.qr || !c.qr[0]) return 0;
  // The facts set the column, in the smallest type: the code is the point here.
  a->facts_font = &lv_font_montserrat_ext_14;
  const lv_coord_t keep = textKeep(*a, c, a->facts_font);
  const lv_coord_t gap = dots(LR_GAP_PX);
  lv_coord_t box = a->bottom - a->y;
  if (box > a->w - keep - gap) box = a->w - keep - gap;
  if (box > a->qr_max) box = a->qr_max;
  if (box < dots(LR_QR_MIN_PX)) { a->facts_font = nullptr; return 0; }
  const lv_coord_t code = drawQr(nullptr, 0, 0, box, c.qr);
  if (!code) { a->facts_font = nullptr; return 0; }
  drawQr(canvas, a->x + a->w - code, a->y + (a->bottom - a->y - code) / 2, code, c.qr);
  a->w -= code + gap;
  return code;
}

// The big code on an upright label: across the top, at most half its height,
// the text under it. Moves the area's top below it and returns its edge.
static lv_coord_t drawTopQr(lv_obj_t* canvas, LabelArea* a, const LabelContent& c) {
  if (!c.qr || !c.qr[0]) return 0;
  a->facts_font = &lv_font_montserrat_ext_14;
  lv_coord_t box = LV_MIN(a->w, (a->bottom - a->y) / 2);
  box = LV_MIN(box, a->qr_max);
  const lv_coord_t code = box >= dots(LR_QR_MIN_PX) ? drawQr(nullptr, 0, 0, box, c.qr) : 0;
  if (!code) { a->facts_font = nullptr; return 0; }
  drawQr(canvas, a->x + (a->w - code) / 2, a->y, code, c.qr);
  a->y += code + dots(LR_GAP_PX);
  return code;
}

// Every label the scale prints, laid out as `layout` says.
static bool renderLabel(const LabelPrinterConfig& printer, const LabelLayout& layout,
                        const LabelContent& c, LabelRaster* out) {
  if (!out || !c.d) return false;
  *out = LabelRaster{};
  const uint16_t content_w = labelPrinterContentWidth(printer.model, printer.media_width_mm);
  const uint16_t h = labelPrinterDotsForMm(printer.model, printer.media_length_mm);
  const uint16_t row_w = labelPrinterRasterWidth(printer.model, printer.media_width_mm);
  if (!row_w || !h || content_w > row_w) return false;
  s_dpi = labelPrinterProfile(printer.model).dpi;

  s_turned = labelLayoutOption(layout, LO_PORTRAIT);
  // The canvas is the label as it reads: upright, the roll's length across.
  const uint16_t cw = s_turned ? h : content_w;
  const uint16_t ch = s_turned ? content_w : h;
  const size_t buf_bytes = LV_CANVAS_BUF_SIZE_TRUE_COLOR(cw, ch);
  void* buf = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
  if (!buf) { logSD("Label: no PSRAM for the canvas"); return false; }

  // An unloaded screen as the parent: the canvas and the QR code exist only
  // to be read back; nothing draws them, and the delete at the end takes
  // both. lv_scr_load() is never called.
  lv_obj_t* parent = lv_obj_create(NULL);
  lv_obj_t* canvas = lv_canvas_create(parent);
  lv_canvas_set_buffer(canvas, buf, cw, ch, LV_IMG_CF_TRUE_COLOR);
  lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);

  const lv_coord_t M = dots(LR_MARGIN_PX);
  const uint8_t preset = layout.preset;
  const bool standard = preset == LABEL_PRESET_STANDARD;
  const lv_coord_t qr_max = labelPrinterDotsForMm(printer.model,
      preset == LABEL_PRESET_MATERIAL_BIG ? LR_QR_SMALL_MM : LR_QR_MAX_MM);
  LabelArea area{ M, M, (lv_coord_t)(cw - 2 * M), (lv_coord_t)(ch - M), qr_max, !standard,
                  nullptr, !c.qr || !c.qr[0] };
  LabelContent body = c;
  lv_coord_t side_qr = 0;
  if (preset == LABEL_PRESET_BIG_QR) {
    side_qr = s_turned ? drawTopQr(canvas, &area, c) : drawSideQr(canvas, &area, c);
    if (side_qr) body.qr = nullptr;
  }
  // The larger sizes need room both ways: a 30 x 40 is tall but narrow, and
  // there the header blocks would eat what the code needs.
  const bool tall = ch >= dots(LR_TALL_PX) && area.w + 2 * M >= dots(LR_TALL_PX);
  area.y = preset == LABEL_PRESET_MATERIAL_BIG ? drawMaterialHeader(canvas, area, *c.d, layout)
                                               : drawHeader(canvas, area, tall, *c.d, layout);
  const LabelBody placed = drawBody(canvas, area, tall, body);

  const uint16_t x0 = labelPrinterContentX(printer);
  const bool ok = packCanvas(canvas, content_w, h, row_w, x0, out);
  lv_obj_del(parent);
  heap_caps_free(buf);
  logSDf("Label: %s %ux%u at dot %u of a %u dot row, qr=%d %s, facts in %s, %s", c.what,
         (unsigned)content_w, (unsigned)h, (unsigned)x0, (unsigned)row_w,
         side_qr ? (int)side_qr : (int)placed.qr,
         side_qr ? (s_turned ? "top" : "side") : placed.beside ? "beside" : "below",
         placed.facts, ok ? "ok" : "failed");
  return ok;
}

bool labelRenderTest(const LabelPrinterConfig& printer, LabelRaster* out) {
  // Says what it is at a glance: the project on top, "test print" in the
  // band, the printer, the stock and the build as the title, and next to the
  // code to Ko-fi one sentence about it (Nikolai, 25.09.2026).
  SpoolLabelData d{};
  snprintf(d.vendor, sizeof(d.vendor), "SpoolmanScale");
  copyT(d.material, sizeof(d.material), STR_LBL_TEST_BAND);
  // The build, as short as it can be said: "beta.79" of a pre-release, the
  // whole version of a release.
  const char* dash = strrchr(FW_VERSION, '-');
  snprintf(d.name, sizeof(d.name), "%s  %ux%u  %s", labelPrinterProfile(printer.model).name,
           (unsigned)printer.media_width_mm, (unsigned)printer.media_length_mm,
           dash ? dash + 1 : FW_VERSION);
  const char* lines[] = { T(STR_LBL_TEST_DONATE) };
  // Always the standard arrangement: the test print checks the printer, not
  // the template.
  const LabelContent c{ &d, lines, 1, true, "https://" DONATION_URL, "test" };
  return renderLabel(printer, labelLayoutDefault(), c, out);
}

// The dot of millimetre v on the calibration ruler, counted from the label's
// first dot at `centre`, negative to the left.
static lv_coord_t rulerDot(LabelPrinterModel model, lv_coord_t centre, int v) {
  const lv_coord_t d = labelPrinterDotsForMm(model, (uint16_t)(v < 0 ? -v : v));
  return v < 0 ? centre - d : centre + d;
}

bool labelRenderCalibration(const LabelPrinterConfig& printer, LabelRaster* out) {
  if (!out) return false;
  *out = LabelRaster{};
  const uint16_t content_w = labelPrinterContentWidth(printer.model, printer.media_width_mm);
  const uint16_t h = labelPrinterDotsForMm(printer.model, printer.media_length_mm);
  const uint16_t row_w = labelPrinterRasterWidth(printer.model, printer.media_width_mm);
  if (!row_w || !h || content_w > row_w) return false;
  s_dpi = labelPrinterProfile(printer.model).dpi;
  s_turned = false;

  // The whole row, not just the label: the ruler has to run under the roll
  // wherever it sits.
  const size_t buf_bytes = LV_CANVAS_BUF_SIZE_TRUE_COLOR(row_w, h);
  void* buf = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
  if (!buf) { logSD("Label: no PSRAM for the canvas"); return false; }
  lv_obj_t* parent = lv_obj_create(NULL);
  lv_obj_t* canvas = lv_canvas_create(parent);
  lv_canvas_set_buffer(canvas, buf, row_w, h, LV_IMG_CF_TRUE_COLOR);
  lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);
  const lv_color_t black = lv_color_black();

  int16_t lo;
  labelPrinterOffsetRange(printer, &lo, nullptr);
  const lv_coord_t centre = -lo;          // the label's first dot at offset 0
  const lv_coord_t x = labelPrinterContentX(printer);
  const int16_t offset = labelPrinterOffset(printer);

  // The ruler across the row, in millimetres of offset, read like any
  // ruler: a tick every millimetre, a longer one every second, and every
  // fourth a number right of a tick that reaches up to it. So the number
  // at the label's left edge is still whole: that is the offset. Numbers
  // where the left edge can be and three more each side, in case a roll sits
  // further out than the head suggests (Nikolai, 30.09.2026); past that the
  // row has ticks alone, and "+48" would not fit the 4 mm anyway.
  int16_t lo_n, hi;
  labelPrinterOffsetRange(printer, &lo_n, &hi);
  const lv_coord_t per_mm = labelPrinterDotsForMm(printer.model, 1);
  const lv_coord_t extra = LC_NUM_EXTRA * LC_NUM_EVERY * per_mm;
  const lv_font_t* num_font = &lv_font_montserrat_ext_16;
  const lv_coord_t num_y = dots(LC_NUM_Y);
  const lv_coord_t base_y = dots(LC_BASE_Y);
  const lv_coord_t tick_w = dots(LC_TICK_PX);
  fillRect(canvas, 0, base_y, row_w, dots(LC_BASE_PX), black);
  // Each tick at its own millimetre's dot rather than per_mm on from the
  // last: at 300 dpi a millimetre is 11.81 dots, and steps of 12 would drift
  // a millimetre across the head.
  for (int v = -(int)((centre + per_mm - 1) / per_mm); ; v++) {
    const lv_coord_t d = rulerDot(printer.model, centre, v);
    if (d >= row_w) break;
    if (d < 0) continue;
    if (v % LC_NUM_EVERY || d - centre > hi + extra || d - centre < lo_n - extra) {
      const lv_coord_t len = dots(v % 2 ? LC_TICK_MM : LC_TICK_2MM);
      fillRect(canvas, d, base_y - len, tick_w, len, black);
      continue;
    }
    fillRect(canvas, d, num_y, tick_w, base_y - num_y, black);
    // No plus sign, as on any ruler: "+20" did not fit the 4 mm in 16 px.
    char num[8];
    snprintf(num, sizeof(num), "%d", v);
    drawLine(canvas, d + tick_w + dots(LC_NUM_GAP), num_y, dots(LC_NUM_W), num_font, black,
             LV_TEXT_ALIGN_LEFT, num);
  }

  // The frame, under the ruler and 1 mm inside where the scale takes the
  // label to be: with the offset right, the same white shows left and right.
  const lv_coord_t M = dots(LR_MARGIN_PX);
  const lv_coord_t F = dots(LC_FRAME_PX);
  const lv_coord_t box_y = base_y + dots(LC_BASE_PX) + dots(LC_BOX_GAP);
  const lv_coord_t box_h = h - M - box_y;
  if (box_h > 2 * F) {
    fillRect(canvas, x + M, box_y, content_w - 2 * M, F, black);
    fillRect(canvas, x + M, h - M - F, content_w - 2 * M, F, black);
    fillRect(canvas, x + M, box_y, F, box_h, black);
    fillRect(canvas, x + content_w - M - F, box_y, F, box_h, black);
  }

  // What to read off, and what was set when this was printed: the photo of
  // a calibration page says both.
  const lv_coord_t gap = dots(LR_GAP_PX);
  const lv_coord_t tx = x + M + F + gap;
  const lv_coord_t tw = content_w - 2 * (M + F + gap);
  const lv_coord_t ty = box_y + F + dots(LC_TEXT_GAP);
  const lv_font_t* big = &lv_font_montserrat_ext_16;
  const lv_font_t* small = &lv_font_montserrat_ext_14;
  const int off_mm = (offset + (offset < 0 ? -per_mm / 2 : per_mm / 2)) / per_mm;
  // Each line only where it fits, so a flatter label keeps ruler and frame.
  const lv_coord_t bottom = h - M - F;
  lv_coord_t ly = ty;
  // Where the label fills the row there is no offset to read off, and the
  // line would ask for one (the M110S, issue #55).
  if (lo_n != hi && ly + lineHeight(big) < bottom) {
    drawLine(canvas, tx, ly, tw, big, black, LV_TEXT_ALIGN_CENTER, T(STR_LBL_CAL_EDGE));
    ly += lineHeight(big) + gap;
  }
  if (ly + lineHeight(small) < bottom) {
    drawLine(canvas, tx, ly, tw, small, black, LV_TEXT_ALIGN_CENTER, "SpoolmanScale");
    ly += lineHeight(small) + gap;
  }
  if (ly + lineHeight(small) < bottom) {
    const char* dash = strrchr(FW_VERSION, '-');
    char info[LABEL_LINE_LEN];
    snprintf(info, sizeof(info), "%s  %ux%u  %s %s%d mm  %s",
             labelPrinterProfile(printer.model).name, (unsigned)printer.media_width_mm,
             (unsigned)printer.media_length_mm, T(STR_W_P_CAL_OFFSET), off_mm > 0 ? "+" : "",
             off_mm, dash ? dash + 1 : FW_VERSION);
    drawLine(canvas, tx, ly, tw, small, black, LV_TEXT_ALIGN_CENTER, info);
  }

  const bool ok = packCanvas(canvas, row_w, h, row_w, 0, out);
  // The label's width, for the check against the loaded stock; the rest of
  // the row carries the ruler on purpose.
  out->content_width = content_w;
  lv_obj_del(parent);
  heap_caps_free(buf);
  logSDf("Label: calibration %ux%u, label at dot %u, offset %d, %s", (unsigned)row_w,
         (unsigned)h, (unsigned)x, (int)offset, ok ? "ok" : "failed");
  return ok;
}

void labelQrForSpool(int spool_id, char* out, size_t n) {
  if (!out || !n) return;
  // FilaMan's label designer defaults its code to /spools/{id}; BamBuddy's
  // labels carry the inventory page. Spoolman's label dialog offers its tag
  // format or the spool page, and its scanner reads both; only the page
  // opens on a phone - "WEB+SPOOLMAN:S-239" left the iPhone with plain text
  // (Nikolai, 30.09.2026). The tag format stays for a scale with no address.
  const char* base = backendBaseUrl();
  if (backendIsFilaMan())       snprintf(out, n, "%s/spools/%d", base, spool_id);
  else if (backendIsBamBuddy()) snprintf(out, n, "%s/inventory?spool=%d", base, spool_id);
  else if (base[0] && strcmp(base, "http://") != 0) snprintf(out, n, "%s/spool/show/%d", base, spool_id);
  else                          snprintf(out, n, "WEB+SPOOLMAN:S-%d", spool_id);
}

bool labelRenderSpool(const LabelPrinterConfig& printer, const LabelLayout& layout,
                      const SpoolLabelData& spool, LabelRaster* out) {
  char qr[LABEL_QR_LEN];
  labelQrForSpool(spool.id, qr, sizeof(qr));
  const int capacity = labelFactCapacity(layout, printer.media_width_mm, printer.media_length_mm);
  LabelFactList l;
  labelSpoolFacts(layout, spool, capacity, &l);
  const LabelContent c{ &spool, l.line_ptrs, l.n, false,
                        labelLayoutHasCode(layout) ? qr : nullptr, "spool", l.facts };
  return renderLabel(printer, layout, c, out);
}
