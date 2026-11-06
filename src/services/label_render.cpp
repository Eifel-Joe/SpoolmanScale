#include "services/label_render.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "app/label_link.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/backend.h"
#include "services/label_draw.h"
#include "services/label_fact_size.h"
#include "services/brother_ql_rules.h"
#include "services/label_edit_rules.h"
#include "services/label_facts.h"
#include "services/label_tape.h"
#include "services/spool_link_store.h"
#include "services/text_util.h"

using namespace labeldraw;


// The label's geometry in dots at 203 dpi, 8 dots to the millimetre, and its
// fonts at that resolution. A finer head prints the same millimetres: every
// length goes through dots() and every font through sized() (label_draw.h).
#define LR_BASE_DPI      203    // the head these lengths and fonts are for
// The 23 x 23 square (Spec V75): a die-cut label with its own white edge, and
// its 20 x 17 mm need every dot for the code (f08, Ziopase c96098d7).
#define LR_MARGIN_SMALL_PX 4
#define LR_COLUMN_GAP_PX  12    // between two columns of facts
// The blocks grow with the roll. 40 x 30 is the base; from LR_TALL_PX in
// both directions the band, the fonts and the fact lines take the larger sizes.
#define LR_TALL_PX       300
#define LR_BAND_H_PX      32    // the material band on the base size, 24 px in it
#define LR_BAND_FONT      (&lv_font_montserrat_ext_24)
#define LR_BAND_THICK_X    1    // extra dots of white across, on the black band
#define LR_BAND_THICK_Y    1    // and down
#define LR_BAND_H_TALL_PX 40
#define LR_BAND_H_BIG_PX  60    // the material-first band, a quarter of 30 mm
#define LR_FACT_LINE_PX   22    // one fact line in the 16 px font
#define LR_FACT_LINE_TALL_PX 28 // one in the 20 px font
#define LR_FACT_LINE_SMALL_PX 19 // one in the 14 px font, next to a big code
#define LR_FACTS_MIN_W   130    // the facts keep this much next to a code
#define LR_QR_MAX_MM      25    // 200 dots at 203 dpi: larger reads no better
#define LR_QR_SMALL_MM    13    // the material-first code: a spool URL at 3 dots
#define LR_QR_MIN_PX      64
#define LR_WRAP_ROWS       4    // what a wrapped paragraph is sized for
#define LR_QR_GOOD_PX     96    // under this, smaller fact lines buy code size
#define LR_FACT_STYLES_MAX 11   // the most styles a head's list may have
#define LR_FACTS_LOG_LEN  24    // "two lines of 40 px" and its end, with room

void labelRasterFree(LabelRaster* image) {
  if (!image) return;
  free(image->pixels);
  *image = LabelRaster{};
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
  // A name one line would cut goes on two in this font; null: it is cut.
  const lv_font_t* wrap_font;
  // A roll under 30 x 30 mm (labelMediaSmall()): a smaller margin, and no
  // gap over a code under no fact line (gapOverCode()).
  bool small;
  // Continuous tape: the blocks start at the top instead of the middle of
  // the height, which is the longest label there may be (endlessLines()).
  bool top;
};

// One of two columns side by side.
static lv_coord_t columnWidth(const LabelArea& a) { return (a.w - dots(LR_COLUMN_GAP_PX)) / 2; }

// Where the body put the code and how it set the facts, for the log:
// "small lines", "28 px lines", "two lines of 36 px"; and whether every fact
// line stands whole (the length search of uprightLength() asks).
struct LabelBody { lv_coord_t qr; bool beside; char facts[LR_FACTS_LOG_LEN]; bool whole; };

// The name as the area sets it on two lines: one line would cut it even in
// the wrap font, and it breaks into two that fit. False: one line.
static bool nameOnTwoLines(const LabelArea& a, const char* name, char* first, char* second) {
  return a.wrap_font && !fitsLine(name, a.wrap_font, a.w) &&
         splitTwoLines(name, a.wrap_font, a.w, first, second);
}

// The filament's name: one line in `f`, as large as it fit; where the area
// has a wrap font and `f` would cut it, one line in the wrap font if that
// holds it, else two. Returns the y under it.
static lv_coord_t drawName(lv_obj_t* canvas, const LabelArea& a, lv_coord_t y,
                           const lv_font_t* f, const char* name) {
  const lv_color_t black = lv_color_black();
  const lv_coord_t gap = dots(LR_GAP_PX);
  char first[LABEL_LINE_LEN], second[LABEL_LINE_LEN];
  const lv_font_t* w = a.wrap_font;
  if (!fitsLine(name, f, a.w) && nameOnTwoLines(a, name, first, second)) {
    drawLine(canvas, a.x, y, a.w, w, black, LV_TEXT_ALIGN_CENTER, first);
    drawLine(canvas, a.x, y + lineHeight(w), a.w, w, black, LV_TEXT_ALIGN_CENTER, second);
    return y + 2 * lineHeight(w) + gap;
  }
  if (w && !fitsLine(name, f, a.w) && fitsLine(name, w, a.w)) f = w;
  drawLine(canvas, a.x, y, a.w, f, black, LV_TEXT_ALIGN_CENTER, name);
  return y + lineHeight(f) + gap;
}

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
    // No 44 px on the larger sizes: that one line cost 85 KB of flash. A
    // name on two lines takes the room of the largest size.
    char l1[LABEL_LINE_LEN], l2[LABEL_LINE_LEN];
    const lv_font_t* f = nameOnTwoLines(a, d.name, l1, l2) ? fitFont(d.vendor, a.w, base + 1, 2)
                                                           : fitFont(d.vendor, a.w, base, 3);
    drawLine(canvas, a.x, y, a.w, f, black, LV_TEXT_ALIGN_CENTER, d.vendor);
    y += lineHeight(f) + dots(LR_GAP_PX);
  }
  if (d.material[0] && labelLayoutHas(layout, LF_MATERIAL)) {
    // The square has no band: it carries the code alone (labelLayoutForMedia()).
    const lv_font_t* f = tall ? &lv_font_montserrat_ext_28 : LR_BAND_FONT;
    const lv_coord_t band_h = dots(tall ? LR_BAND_H_TALL_PX : LR_BAND_H_PX);
    // Plain: the same line in black on white, for a roll that smears a band.
    const bool plain = labelLayoutOption(layout, LO_MATERIAL_PLAIN);
    if (!plain) fillRect(canvas, a.x, y, a.w, band_h, black);
    const lv_coord_t ty = y + (band_h - lineHeight(f)) / 2;
    // White on black is drawn heavier still: the black around it bleeds into
    // the strokes on a thermal head, and the base size's 22 px came out thin.
    const lv_coord_t thick_x = plain ? 0 : dots(LR_BAND_THICK_X), thick_y = plain ? 0 : dots(LR_BAND_THICK_Y);
    for (lv_coord_t dy = 0; dy <= thick_y; dy++)
      for (lv_coord_t dx = 0; dx <= thick_x; dx++)
        drawLine(canvas, a.x + dx, ty + dy, a.w, f, plain ? black : lv_color_white(),
                 LV_TEXT_ALIGN_CENTER, d.material);
    y += band_h + dots(LR_GAP_PX);
  }
  if (d.name[0] && labelLayoutHas(layout, LF_NAME)) {
    static const lv_font_t* const base[] = { &lv_font_montserrat_ext_28, &lv_font_montserrat_ext_24, &lv_font_montserrat_ext_20 };
    static const lv_font_t* const big[]  = { &lv_font_montserrat_ext_36, &lv_font_montserrat_ext_28, &lv_font_montserrat_ext_24, &lv_font_montserrat_ext_20 };
    const lv_font_t* f = tall ? fitFont(d.name, a.w, big, 4) : fitFont(d.name, a.w, base, 3);
    y = drawName(canvas, a, y, f, d.name);
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
    if (text == d.name) { y = drawName(canvas, a, y, f, text); continue; }
    drawLine(canvas, a.x, y, a.w, f, black, LV_TEXT_ALIGN_CENTER, text);
    y += lineHeight(f) + gap;
  }
  return y;
}

// Either header, as the arrangement has it. With no canvas it only measures.
static lv_coord_t drawHead(lv_obj_t* canvas, const LabelArea& a, bool tall,
                           const SpoolLabelData& d, const LabelLayout& layout) {
  return layout.preset == LABEL_PRESET_MATERIAL_BIG ? drawMaterialHeader(canvas, a, d, layout)
                                                    : drawHeader(canvas, a, tall, d, layout);
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

// The space over a code drawn under the facts. None on a small roll with no
// fact line: nothing stands above it, and the square needs every line for
// the code (acceptance of v0.8.7-beta.7, round 1).
static lv_coord_t gapOverCode(const LabelArea& a, int rows) {
  return a.small && rows == 0 ? 0 : dots(LR_GAP_PX);
}

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
    lv_coord_t qr_below = avail_h - rows * p.line - gapOverCode(a, rows);
    if (qr_below > a.w) qr_below = a.w;
    // Continuous tape, whose height is the longest label there may be: the
    // code beside the lines on the wide tape, under them on the narrow one,
    // centred under the header when there are none (Joerg 05.11.2026).
    p.beside = a.top ? tall && rows > 0 : qr_beside >= qr_below;
    p.qr = p.beside ? qr_beside : qr_below;
    if (p.qr > a.qr_max) p.qr = a.qr_max;
    if (p.qr >= dots(LR_QR_GOOD_PX) || p.font != &lv_font_montserrat_ext_20) break;
    p.font = &lv_font_montserrat_ext_16;
    p.line = dots(LR_FACT_LINE_PX);
  }
  // The code comes out in whole dots per module and is mostly smaller than its
  // box: the text column reaches to the code, not to the box, which gave a
  // date line on 40 x 30 the room it was cut short of.
  if (p.qr >= dots(LR_QR_MIN_PX) && c.qr && c.qr[0]) p.code = drawQr(nullptr, 0, 0, p.qr, c.qr, true);
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
    const lv_coord_t py = p.beside && !a.top && text_h < avail_h ? facts_top + (avail_h - text_h) / 2
                                                                 : facts_top;
    drawParagraph(canvas, a.x, py, facts_w, fact_font, c.lines[0]);
  } else {
    // Next to the code the lines stand in the middle of the height they share.
    const lv_coord_t block = c.n * p.line;
    const lv_coord_t fy = p.beside && !a.top && block < avail_h ? facts_top + (avail_h - block) / 2
                                                                : facts_top;
    for (int i = 0; i < c.n; i++)
      drawLine(canvas, a.x, fy + i * p.line, facts_w, fact_font,
               lv_color_black(), LV_TEXT_ALIGN_LEFT, c.lines[i]);
  }
  if (p.code) {
    const lv_coord_t qx = p.beside ? a.x + a.w - p.code : a.x + (a.w - p.code) / 2;
    // The square, which carries the code alone, has it in the middle.
    const lv_coord_t qy = p.beside ? (a.top ? facts_top : facts_top + (avail_h - p.code) / 2)
                        : a.small && rows == 0 ? facts_top + (avail_h - p.code) / 2
                                               : facts_top + text_h + gapOverCode(a, rows);
    drawQr(canvas, qx, qy, p.code, c.qr);
  }
}

// The larger sizes for the facts, tried in this order: by the size the
// value prints in, from large to small, either two lines each (the caption
// small, the value large under it) or one line, the two-line form first
// where both print in the same size. A fact with no value, the project
// name, stays in the small font. The largest is for the tall sizes only; on
// the base size no fact outgrows the filament's name. `px` is the size the
// value prints in on the list's head.
// Root: one list for both heads named the one-line sizes by 203 dpi fonts,
//   24 and 20. A 300 dpi head prints those as 36 and 30, and below 30 came
//   the small lines (24): six facts two dots too wide for 30 fell to them,
//   where 28 would have held them whole (V45). With the two-line rows all
//   first, six short lines printed one-line 34 and five of them two-line
//   30: smaller with one line fewer.
// Fix: a list per head, ordered by print size (Joerg 29.10.2026: one line
//   fewer never prints smaller). The two-line rows are the ones of before;
//   one line goes in every even print size from the largest one-line size
//   down to what the big-code arrangement's 14 px small lines leave, in the
//   PRINTED_ fonts that print as themselves (label_draw.h). The usual small
//   lines are larger, and planLarger() skips what is not. The last row is
//   the small lines once more, for a label without a code only: in two
//   columns they hold what one column would push off the label.
// NOT-TO-DO: one list of 203 dpi fonts for both heads. On a 300 dpi head
//   they print 1.48 times their size, so 26, 28 and 34 cannot be named and
//   the 28 prints as 40. Nor all two-line rows first: they read well, but a
//   label with one line fewer then fell from one-line 34 to two-line 30.
// See test_label_fact_styles.py.
struct FactStyle { const lv_font_t* value; const lv_font_t* caption; bool two_lines; uint8_t px; };
static const FactStyle FACT_STYLES_203[] = {
  { &lv_font_montserrat_ext_28, &lv_font_montserrat_ext_16, true, 28 },
  { &lv_font_montserrat_ext_24, &lv_font_montserrat_ext_14, true, 24 },
  { &PRINTED_24, &lv_font_montserrat_ext_14, false, 24 },
  { &PRINTED_22, &lv_font_montserrat_ext_14, false, 22 },
  { &lv_font_montserrat_ext_20, &lv_font_montserrat_ext_14, true, 20 },
  { &PRINTED_20, &lv_font_montserrat_ext_14, false, 20 },
  { &PRINTED_18, &lv_font_montserrat_ext_14, false, 18 },
  { &PRINTED_16, &lv_font_montserrat_ext_14, false, 16 },
  { &lv_font_montserrat_ext_16, &lv_font_montserrat_ext_14, false, 16 },   // no code only, last
};
static const FactStyle FACT_STYLES_300[] = {
  { &lv_font_montserrat_ext_28, &lv_font_montserrat_ext_16, true, 40 },
  { &lv_font_montserrat_ext_24, &lv_font_montserrat_ext_14, true, 36 },
  { &PRINTED_36, &lv_font_montserrat_ext_14, false, 36 },
  { &PRINTED_34, &lv_font_montserrat_ext_14, false, 34 },
  { &PRINTED_32, &lv_font_montserrat_ext_14, false, 32 },
  { &lv_font_montserrat_ext_20, &lv_font_montserrat_ext_14, true, 30 },
  { &PRINTED_30, &lv_font_montserrat_ext_14, false, 30 },
  { &PRINTED_28, &lv_font_montserrat_ext_14, false, 28 },
  { &PRINTED_26, &lv_font_montserrat_ext_14, false, 26 },
  { &PRINTED_24, &lv_font_montserrat_ext_14, false, 24 },
  { &lv_font_montserrat_ext_16, &lv_font_montserrat_ext_14, false, 24 },   // no code only, last
};
static_assert(sizeof(FACT_STYLES_203) / sizeof(FACT_STYLES_203[0]) <= LR_FACT_STYLES_MAX,
              "planLarger() measures at most LR_FACT_STYLES_MAX styles");
static_assert(sizeof(FACT_STYLES_300) / sizeof(FACT_STYLES_300[0]) <= LR_FACT_STYLES_MAX,
              "planLarger() measures at most LR_FACT_STYLES_MAX styles");

// The list of the head being drawn for, the one whose fonts sized() leaves
// as they are or the finer one, and its length. Its last row is the one
// only a label without a code may take.
static const FactStyle* factStyles(int* count) {
  if (dpi() <= LR_BASE_DPI) {
    *count = (int)(sizeof(FACT_STYLES_203) / sizeof(FACT_STYLES_203[0]));
    return FACT_STYLES_203;
  }
  *count = (int)(sizeof(FACT_STYLES_300) / sizeof(FACT_STYLES_300[0]));
  return FACT_STYLES_300;
}

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

// The facts larger, when the label has the room: of the head's styles that
// are larger than the small lines (and, on a label that is not tall, no
// larger than what 24 prints in), the first in the list's order whose lines
// fit the height, whose lines fit the width whole, and that leaves the code
// no smaller than the small lines did. So a label with few fields reads
// larger, and one with all of them prints as before.
// Root: the loop compared each style itself by the line heights of the
//   203 dpi fonts. Those are not the sizes a 300 dpi head prints, and a
//   PRINTED_ font is on no such scale; the choice is labelFactPick()'s now.
// Fix: each style compared by lineHeight(), the size it prints in, and
//   measured into one LabelFactFit, the plan it stands for in a second array
//   of the same fixed size. Where one column does not fit and the label has
//   no code, two columns: the width both take side by side, 2 w + gap (that
//   is within a.w exactly when w is within columnWidth()), the taller column,
//   no code. Else one column, its code measured only where the style spells
//   and fits; 0 for the others, which cannot win anyway. Measuring stops at
//   the first style the rule passes on its own: none after it can be
//   chosen, so the code is encoded about once per pass, as before V45.
// NOT-TO-DO: measuring every style before asking the rule: each code
//   measured encodes the QR code once more, for styles that cannot win.
//   Nor reordering here: the list is by print size, two lines first on
//   equal size (Joerg 29.10.2026: one line fewer never prints smaller).
// See test_label_fact_styles.py and test_label_fact_size.py.
static bool planLarger(const LabelArea& a, bool tall, const LabelContent& c,
                       const BodyPlan& one, FactsPlan* out) {
  if (c.wrap || !c.facts || c.n == 0) return false;
  const lv_coord_t avail_h = a.bottom - a.y;
  const LabelFactRoom room{ (int16_t)a.w, (int16_t)avail_h, (int16_t)one.code };
  int count = 0;
  const FactStyle* styles = factStyles(&count);
  const FactStyle* no_code = &styles[count - 1];
  LabelFactFit fits[LR_FACT_STYLES_MAX];
  FactsPlan plans[LR_FACT_STYLES_MAX];
  int n = 0;
  for (int i = 0; i < count; i++) {
    const FactStyle& s = styles[i];
    if (&s == no_code ? !a.columns : lineHeight(s.value) <= lineHeight(one.font)) continue;
    if (!tall && lineHeight(s.value) > lineHeight(&lv_font_montserrat_ext_24)) continue;
    const lv_coord_t w = factsWidth(c, s);
    const lv_coord_t h = factsHeight(c, s, 0, c.n);
    LabelFactFit& f = fits[n];
    f.spells = w != 0;
    if ((w > a.w || h > avail_h) && a.columns && !one.code && c.n >= 2) {
      // Two columns where the arrangement has no code to share the width with.
      const int half = (c.n + 1) / 2;
      const lv_coord_t h2 = LV_MAX(factsHeight(c, s, 0, half), factsHeight(c, s, half, c.n));
      f.width = (int16_t)(2 * w + dots(LR_COLUMN_GAP_PX));
      f.height = h2;
      f.code = 0;
      plans[n++] = FactsPlan{ &s, 0, h2, 2 };
    } else {
      lv_coord_t code = 0;
      if (one.code && f.spells && w <= a.w && h <= avail_h) {
        lv_coord_t box = one.beside ? a.w - w - dots(LR_GAP_PX) : avail_h - h - dots(LR_GAP_PX);
        box = LV_MIN(box, one.beside ? avail_h : a.w);
        box = LV_MIN(box, a.qr_max);
        code = box >= dots(LR_QR_MIN_PX) ? drawQr(nullptr, 0, 0, box, c.qr, true) : 0;
      }
      f.width = w;
      f.height = h;
      f.code = code;
      plans[n++] = FactsPlan{ &s, code, h, 1 };
    }
    if (labelFactPick(&fits[n - 1], 1, room) == 0) break;
  }
  const int pick = labelFactPick(fits, n, room);
  if (pick < 0) return false;
  *out = plans[pick];
  return true;
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
  // Continuous tape: from the top, the label ends where its content does.
  const lv_coord_t y = beside && !a.top && p.text_h < avail_h ? a.y + (avail_h - p.text_h) / 2 : a.y;
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
    const lv_coord_t qy = beside ? (a.top ? a.y : a.y + (avail_h - p.code) / 2)
                                 : a.y + p.text_h + dots(LR_GAP_PX);
    drawQr(canvas, qx, qy, p.code, c.qr);
  }
}

// Whether the small lines stand whole: each within the width next to the
// code, all of them within the height.
static bool oneLineWhole(const LabelArea& a, const LabelContent& c, const BodyPlan& p) {
  if (c.wrap) return true;
  const lv_coord_t w = (p.beside && p.code) ? a.w - p.code - dots(LR_GAP_PX) : a.w;
  for (int i = 0; i < c.n; i++)
    if (!fitsLine(c.lines[i], p.font, w)) return false;
  const lv_coord_t text = c.n * p.line;
  const lv_coord_t need = p.beside ? LV_MAX(text, p.code)
                                   : text + (p.code ? p.code + gapOverCode(a, c.n) : 0);
  return need <= a.bottom - a.y;
}

static LabelBody drawBody(lv_obj_t* canvas, const LabelArea& a, bool tall,
                          const LabelContent& c) {
  const BodyPlan one = planOneLine(a, tall, c);
  FactsPlan larger;
  LabelBody body{};
  body.beside = one.beside;
  body.whole = true;   // a larger style is only chosen with its lines whole
  if (planLarger(a, tall, c, one, &larger)) {
    drawLarger(canvas, a, c, larger, one.beside);
    body.qr = larger.code;
    // The size the values print in, as the head's list has it (V45).
    if (larger.style->two_lines)
      snprintf(body.facts, sizeof(body.facts), "two lines of %u px", (unsigned)larger.style->px);
    else
      snprintf(body.facts, sizeof(body.facts), "%u px lines", (unsigned)larger.style->px);
    return body;
  }
  drawOneLine(canvas, a, c, one);
  body.qr = one.code ? one.qr : (lv_coord_t)0;
  body.whole = oneLineWhole(a, c, one);
  snprintf(body.facts, sizeof(body.facts), "small lines");
  return body;
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
  const lv_coord_t code = drawQr(nullptr, 0, 0, box, c.qr, true);
  if (!code) { a->facts_font = nullptr; return 0; }
  const lv_coord_t qy = a->top ? a->y : a->y + (a->bottom - a->y - code) / 2;
  drawQr(canvas, a->x + a->w - code, qy, code, c.qr);
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
  const lv_coord_t code = box >= dots(LR_QR_MIN_PX) ? drawQr(nullptr, 0, 0, box, c.qr, true) : 0;
  if (!code) { a->facts_font = nullptr; return 0; }
  drawQr(canvas, a->x + (a->w - code) / 2, a->y, code, c.qr);
  a->y += code + dots(LR_GAP_PX);
  return code;
}

// The code the body draws once the header ends at a.y, 0 for none, and
// whether every fact line stays whole.
static lv_coord_t bodyCode(const LabelArea& a, bool tall, const LabelContent& c, bool* fits) {
  const BodyPlan one = planOneLine(a, tall, c);
  FactsPlan larger;
  *fits = true;
  if (planLarger(a, tall, c, one, &larger)) return larger.code;
  const lv_coord_t text = (c.wrap ? LR_WRAP_ROWS : c.n) * one.line;
  const int rows = c.wrap ? LR_WRAP_ROWS : c.n;
  const lv_coord_t need = one.beside ? LV_MAX(text, one.code)
                                     : text + (one.code ? one.code + gapOverCode(a, rows) : 0);
  *fits = need <= a.bottom - a.y;
  return one.code;
}

// The font a name one line would cut is set in instead, on one line or two;
// null to cut it as before. The largest of three that leaves every fact
// whole and the code as large as the cut line did. Measured, nothing drawn.
static const lv_font_t* nameWrapFont(const LabelArea& a, bool tall, const LabelContent& c,
                                     const LabelLayout& layout) {
  const SpoolLabelData& d = *c.d;
  if (!d.name[0] || !labelLayoutHas(layout, LF_NAME)) return nullptr;
  // The smallest size the header sets the name in on one line.
  const lv_font_t* smallest = layout.preset == LABEL_PRESET_MATERIAL_BIG
      ? &lv_font_montserrat_ext_16 : &lv_font_montserrat_ext_20;
  if (fitsLine(d.name, smallest, a.w)) return nullptr;
  LabelArea plain = a;
  plain.wrap_font = nullptr;
  plain.y = drawHead(nullptr, plain, tall, d, layout);
  bool fits = false;
  const lv_coord_t code = bodyCode(plain, tall, c, &fits);
  static const lv_font_t* const WRAP_FONTS[] = {
    &lv_font_montserrat_ext_20, &lv_font_montserrat_ext_16, &lv_font_montserrat_ext_14,
  };
  for (const lv_font_t* f : WRAP_FONTS) {
    LabelArea trial = a;
    trial.wrap_font = f;
    trial.y = drawHead(nullptr, trial, tall, d, layout);
    if (trial.y == plain.y) continue;   // no break that fits two lines
    const lv_coord_t trial_code = bodyCode(trial, tall, c, &fits);
    if (fits && trial_code >= code) return f;
  }
  return nullptr;
}

// How a label sits on its canvas, decided before anything is drawn.
struct LabelShape {
  bool endless;   // continuous tape
  bool turned;    // upright: the canvas is the label as it reads
  bool wide;      // the label at least LR_TALL_PX across the head
  uint16_t ch;    // the canvas's height in dots
};

// What laying a label out gave: the body, the big code beside or over the
// text (0: none), and where the header ended.
struct LaidOut { LabelBody body; lv_coord_t side_qr; lv_coord_t head_y; };

// The blocks into `area`: the big code, the header, the facts and the code.
// With no canvas it only measures.
static LaidOut layOut(lv_obj_t* canvas, LabelArea area, const LabelContent& c,
                      const LabelLayout& layout, const LabelShape& s) {
  LaidOut o{};
  LabelContent body = c;
  // The narrow continuous tape flat has the code under the lines in every
  // arrangement: beside them the 38 mm leave it too little width. Upright
  // the label is as long as it needs, and the code goes beside.
  if (layout.preset == LABEL_PRESET_BIG_QR && (!s.endless || s.wide || s.turned)) {
    o.side_qr = s.turned && !s.endless ? drawTopQr(canvas, &area, c) : drawSideQr(canvas, &area, c);
    if (o.side_qr) body.qr = nullptr;
  }
  // The larger sizes need room both ways: a 30 x 40 is tall but narrow, and
  // there the header blocks would eat what the code needs. On continuous tape
  // flat only the width says it: its length follows the content; upright the
  // tape's width is the height and says it as a roll's length does.
  const bool tall = ((s.endless && !s.turned) || s.ch >= dots(LR_TALL_PX)) &&
                    area.w + 2 * area.x >= dots(LR_TALL_PX);
  area.wrap_font = nameWrapFont(area, tall, body, layout);
  area.y = drawHead(canvas, area, tall, *c.d, layout);
  o.head_y = area.y;
  o.body = drawBody(canvas, area, tall, body);
  return o;
}

// Whether a try prints as well as the label on the longest length: the
// header in the same sizes, the facts in the same style and whole, no code
// smaller.
static bool asGoodAs(const LaidOut& t, const LaidOut& best) {
  return t.head_y == best.head_y && t.side_qr >= best.side_qr && t.body.qr >= best.body.qr &&
         (t.body.whole || !best.body.whole) && strcmp(t.body.facts, best.body.facts) == 0;
}

// The length of an upright label on continuous tape (Spec V77, Joerg
// 06.11.2026): its height is the tape's width, its length the least at which
// it prints as it would on the longest label there may be. Measured, nothing
// drawn.
// Root: flat, a continuous label is drawn on the longest length and cut after
//   its last line of ink (endlessLines()). Upright the length runs across the
//   reading: the header is centred on it and the code stands at its right
//   end, so a cut after the ink would keep the whole length.
// Fix: the length searched by halving between Brother's shortest and longest
//   label, each try laid out without a canvas (layOut()) and held against the
//   longest (asGoodAs()). Ten tries; a try encodes the code a few times.
// NOT-TO-DO: adding up the header's and the facts' text widths here: the
//   fonts, a name on two lines and the code's room are layOut()'s to decide,
//   and a second account of them would drift from it.
static uint16_t uprightLength(LabelArea a, const LabelContent& c, const LabelLayout& layout,
                              const LabelShape& s) {
  const lv_coord_t M = a.x;
  a.w = BROTHER_QL_MAX_LINES - 2 * M;
  const LaidOut best = layOut(nullptr, a, c, layout, s);
  uint16_t lo = BROTHER_QL_MIN_LINES, hi = BROTHER_QL_MAX_LINES;
  while (lo < hi) {
    const uint16_t mid = (uint16_t)((lo + hi) / 2);
    a.w = mid - 2 * M;
    if (asGoodAs(layOut(nullptr, a, c, layout, s), best)) hi = mid;
    else lo = (uint16_t)(mid + 1);
  }
  return hi;
}

// Every label the scale prints, laid out as `layout` says.
static bool renderLabel(const LabelPrinterConfig& printer, const LabelLayout& layout,
                        const LabelContent& c, LabelRaster* out) {
  if (!out || !c.d) return false;
  *out = LabelRaster{};
  const uint16_t content_w = labelPrinterContentWidth(printer.model, printer.media_width_mm);
  // Continuous tape has no length of its own: flat the longest there may be,
  // cut to the content after drawing (label_draw.h endlessLines()); upright
  // the one uprightLength() finds.
  const bool endless = labelPrinterMediaContinuous(printer.model, printer.media_width_mm,
                                                   printer.media_length_mm);
  uint16_t h = endless ? BROTHER_QL_MAX_LINES
                       : labelPrinterContentHeight(printer.model, printer.media_width_mm,
                                                   printer.media_length_mm);
  const uint16_t row_w = labelPrinterRasterWidth(printer.model, printer.media_width_mm);
  if (!row_w || !h || content_w > row_w) return false;
  setDpi(labelPrinterProfile(printer.model).dpi);

  // Upright, continuous tape too (Spec V77): the canvas is the label as it
  // reads, the roll's length across (the square is flat, labelLayoutForMedia()).
  const bool turned = labelLayoutOption(layout, LO_PORTRAIT);
  setTurned(turned);
  const LabelShape shape{ endless, turned, content_w >= dots(LR_TALL_PX),
                          turned ? content_w : h };
  const bool small = labelMediaSmall(printer.media_width_mm, printer.media_length_mm);
  const lv_coord_t M = dots(small ? LR_MARGIN_SMALL_PX : LR_MARGIN_PX);
  const uint8_t preset = layout.preset;
  const lv_coord_t qr_max = labelPrinterDotsForMm(printer.model,
      preset == LABEL_PRESET_MATERIAL_BIG ? LR_QR_SMALL_MM : LR_QR_MAX_MM);
  // Flat on continuous tape the blocks start at the top of the longest label;
  // upright they stand in the middle of the tape's width, as on any roll.
  LabelArea area{ M, M, 0, (lv_coord_t)(shape.ch - M), qr_max, preset != LABEL_PRESET_STANDARD,
                  nullptr, !c.qr || !c.qr[0], nullptr, small, endless && !turned };
  if (endless && turned) h = uprightLength(area, c, layout, shape);
  const uint16_t cw = turned ? h : content_w;
  area.w = (lv_coord_t)(cw - 2 * M);
  const size_t buf_bytes = LV_CANVAS_BUF_SIZE_TRUE_COLOR(cw, shape.ch);
  void* buf = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
  if (!buf) { logSD("Label: no PSRAM for the canvas"); return false; }

  // An unloaded screen as the parent: the canvas and the QR code exist only
  // to be read back; nothing draws them, and the delete at the end takes
  // both. lv_scr_load() is never called.
  lv_obj_t* parent = lv_obj_create(NULL);
  lv_obj_t* canvas = lv_canvas_create(parent);
  lv_canvas_set_buffer(canvas, buf, cw, shape.ch, LV_IMG_CF_TRUE_COLOR);
  lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);

  const LaidOut placed = layOut(canvas, area, c, layout, shape);
  if (endless && !turned) h = endlessLines(canvas, cw, shape.ch, M);
  // The square carries the code alone (Spec V76): a label with none, such as
  // a filament's without a page to open, or one whose code does not fit,
  // would come out blank. Refused instead; the label page says why.
  if (small && !placed.body.qr) {
    logSDf("Label: %s has no code for the 23 x 23 square, nothing to print", c.what);
    lv_obj_del(parent);
    heap_caps_free(buf);
    return false;
  }

  const uint16_t x0 = labelPrinterContentX(printer);
  const bool ok = packCanvas(canvas, content_w, h, row_w, x0, out);
  lv_obj_del(parent);
  heap_caps_free(buf);
  logSDf("Label: %s %ux%u at dot %u of a %u dot row, qr=%d %s, facts in %s, %s", c.what,
         (unsigned)content_w, (unsigned)h, (unsigned)x0, (unsigned)row_w,
         placed.side_qr ? (int)placed.side_qr : (int)placed.body.qr,
         placed.side_qr ? (turned && !endless ? "top" : "side")
                        : placed.body.beside ? "beside" : "below",
         placed.body.facts, ok ? "ok" : "failed");
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
  char stock[LABEL_LINE_LEN];
  labelMediaShort(printer.model, printer.media_width_mm, printer.media_length_mm, stock,
                  sizeof(stock));
  snprintf(d.name, sizeof(d.name), "%s  %s  %s", labelPrinterProfile(printer.model).name, stock,
           dash ? dash + 1 : FW_VERSION);
  // The tape has no room for the sentence and the code: the three lines say
  // what it is, the size and the build in the name, the printer left out.
  if (labelPrinterIsTape(printer.model)) {
    snprintf(d.name, sizeof(d.name), "%s  %s", stock, dash ? dash + 1 : FW_VERSION);
    return labelRenderTape(printer, d, false, "test", out);
  }
  const char* lines[] = { T(STR_LBL_TEST_DONATE) };
  // Always the standard arrangement: the test print checks the printer, not
  // the template. Narrowed to the roll like every label; the square keeps the
  // code alone, and no sentence: a wrapped one is sized for four rows even
  // when it is empty, and took the code's room (Ziopase 6d2c27b2).
  const bool small = labelMediaSmall(printer.media_width_mm, printer.media_length_mm);
  const LabelContent c{ &d, lines, small ? 0 : 1, !small, "https://" DONATION_URL, "test" };
  return renderLabel(printer,
                     labelLayoutForMedia(labelLayoutDefault(), printer.media_width_mm,
                                         printer.media_length_mm), c, out);
}

void labelQrForSpool(int spool_id, char* out, size_t n) {
  if (!out || !n) return;
  // FilaMan's label designer defaults its code to /spools/{id}; BamBuddy's
  // labels carry the inventory page. Spoolman's label dialog offers its tag
  // format or the spool page, and its scanner reads both; only the page
  // opens on a phone - "WEB+SPOOLMAN:S-239" left the iPhone with plain text
  // (Nikolai, 30.09.2026). The tag format stays for a scale with no address.
  // The address is the server's own for its labels where it has one.
  const char* base = labelLinkBase();
  if (backendIsFilaMan())       snprintf(out, n, "%s/spools/%d", base, spool_id);
  else if (backendIsBamBuddy()) snprintf(out, n, "%s/inventory?spool=%d", base, spool_id);
  else if (base[0] && strcmp(base, "http://") != 0) snprintf(out, n, "%s/spool/show/%d", base, spool_id);
  else                          snprintf(out, n, "WEB+SPOOLMAN:S-%d", spool_id);
}

void labelQrForFilament(const SpoolLabelData& spool, char* out, size_t n) {
  if (!out || !n) return;
  out[0] = '\0';
  // A server that names its spools' pages itself may have no filament page at
  // all: Spooly has none, and a code that opens nothing is worse than no code.
  if (spoolLinkHas(spool.id)) return;
  const int filament_id = spool.filament_id;
  const char* base = labelLinkBase();
  // BamBuddy keeps no filament, and a Spoolman with no address has no page.
  if (filament_id <= 0 || backendIsBamBuddy() || !base[0] || strcmp(base, "http://") == 0) return;
  if (backendIsFilaMan()) snprintf(out, n, "%s/filaments/%d", base, filament_id);
  else                    snprintf(out, n, "%s/filament/show/%d", base, filament_id);
}

// A fact list in PSRAM, null when there is none: at LABEL_MAX_FACTS lines it
// is too large for the stack of the task that draws (label_facts.h, V77).
static LabelFactList* factList() {
  LabelFactList* l = (LabelFactList*)heap_caps_calloc(1, sizeof(LabelFactList), MALLOC_CAP_SPIRAM);
  if (!l) logSD("Label: no PSRAM for the fact lines");
  return l;
}

bool labelRenderFilament(const LabelPrinterConfig& printer, const LabelLayout& layout,
                         const SpoolLabelData& spool, LabelRaster* out) {
  // The tape's own arrangement, without the spool's number: see label_tape.h.
  if (labelPrinterIsTape(printer.model)) return labelRenderTape(printer, spool, false, "filament", out);
  char qr[LABEL_QR_LEN];
  labelQrForFilament(spool, qr, sizeof(qr));
  // No page to open: the label is laid out as one without a code. The
  // template as the roll carries it (the square: the code alone, refused
  // without one in renderLabel()).
  LabelLayout l = labelLayoutForMedia(layout, printer.media_width_mm, printer.media_length_mm);
  if (!qr[0]) l.fields &= ~(1u << LF_QR);
  const int capacity = labelFactCapacity(l, printer.media_width_mm, printer.media_length_mm);
  LabelFactList* f = factList();
  if (!f) return false;
  labelFilamentFacts(l, spool, capacity, f);
  const LabelContent c{ &spool, f->line_ptrs, f->n, false,
                        labelLayoutHasCode(l) ? qr : nullptr, "filament", f->facts };
  const bool ok = renderLabel(printer, l, c, out);
  heap_caps_free(f);
  return ok;
}

bool labelRenderSpool(const LabelPrinterConfig& printer, const LabelLayout& saved,
                      const SpoolLabelData& spool, LabelRaster* out) {
  // The tape's own arrangement: maker, material, name and the number.
  if (labelPrinterIsTape(printer.model)) return labelRenderTape(printer, spool, true, "spool", out);
  // The template as the roll carries it (the square: the code alone).
  const LabelLayout layout = labelLayoutForMedia(saved, printer.media_width_mm,
                                                 printer.media_length_mm);
  char qr[LABEL_QR_LEN];
  // The server's own link where it named one when the spool was read (o18;
  // kept in PSRAM, services/spool_link_store.h), else the one built from the id.
  if (spoolLinkCopy(spool.id, qr, sizeof(qr)))
    logSDf("Label: spool #%d carries its own link, the QR code uses it", spool.id);
  else labelQrForSpool(spool.id, qr, sizeof(qr));
  const int capacity = labelFactCapacity(layout, printer.media_width_mm, printer.media_length_mm);
  LabelFactList* l = factList();
  if (!l) return false;
  labelSpoolFacts(layout, spool, capacity, l);
  const LabelContent c{ &spool, l->line_ptrs, l->n, false,
                        labelLayoutHasCode(layout) ? qr : nullptr, "spool", l->facts };
  const bool ok = renderLabel(printer, layout, c, out);
  heap_caps_free(l);
  return ok;
}

bool labelRenderEdit(const LabelPrinterConfig& printer, const LabelLayout& layout,
                     const LabelEdit& edit, const SpoolLabelData& spool, LabelRaster* out) {
  // The tape's label is fixed (label_tape.h); the edit screen is not offered.
  if (labelPrinterIsTape(printer.model)) return false;
  const LabelLayout narrowed = labelLayoutForMedia(layout, printer.media_width_mm,
                                                   printer.media_length_mm);
  // The lines this roll takes, with the code or without as it will print:
  // rows made for a larger roll keep their first lines (review, finding 1).
  LabelLayout counted = narrowed;
  if (!edit.code || !edit.qr[0]) counted.fields &= ~(1u << LF_QR);
  const int capacity = labelFactCapacity(counted, printer.media_width_mm, printer.media_length_mm);
  SpoolLabelData d = spool;
  LabelFactList* f = factList();
  if (!f) return false;
  const LabelLayout l = labelEditApply(edit, narrowed, capacity, &d, f);
  const LabelContent c{ &d, f->line_ptrs, f->n, false, labelLayoutHasCode(l) ? edit.qr : nullptr,
                        edit.free ? "free" : "edited", f->facts };
  const bool ok = renderLabel(printer, l, c, out);
  heap_caps_free(f);
  return ok;
}
