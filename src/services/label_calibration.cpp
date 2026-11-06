#include "services/label_render.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/label_draw.h"

using namespace labeldraw;

// ============================================================
//  THE CALIBRATION LABEL
//
//  A ruler across the whole print row and a frame where the scale
//  takes the label to be: the number at the label's left edge is the
//  print offset to set. Moved out of label_render.cpp, whose labels
//  lay out a spool, when that file reached its length (v0.8.7-beta.7).
// ============================================================

// Continuous tape has no length of its own, and the calibration page no
// content to measure one by: it takes 24 mm.
#define LR_ENDLESS_CALIB_LINES 284
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

// The dot of millimetre v on the calibration ruler, counted from the label's
// first dot at `centre`, negative to the left.
static lv_coord_t rulerDot(LabelPrinterModel model, lv_coord_t centre, int v) {
  const lv_coord_t d = labelPrinterDotsForMm(model, (uint16_t)(v < 0 ? -v : v));
  return v < 0 ? centre - d : centre + d;
}

bool labelRenderCalibration(const LabelPrinterConfig& printer, LabelRaster* out) {
  if (!out) return false;
  // A tape printer's head is as wide as the tape: there is no offset to
  // read off, and the test label shows where the print sits.
  if (labelPrinterIsTape(printer.model)) return labelRenderTest(printer, out);
  *out = LabelRaster{};
  const uint16_t content_w = labelPrinterContentWidth(printer.model, printer.media_width_mm);
  // Continuous tape: the ruler and the frame on a label of fixed length.
  const uint16_t h = labelPrinterMediaContinuous(printer.model, printer.media_width_mm,
                                                 printer.media_length_mm)
      ? LR_ENDLESS_CALIB_LINES
      : labelPrinterContentHeight(printer.model, printer.media_width_mm, printer.media_length_mm);
  const uint16_t row_w = labelPrinterRasterWidth(printer.model, printer.media_width_mm);
  if (!row_w || !h || content_w > row_w) return false;
  setDpi(labelPrinterProfile(printer.model).dpi);
  setTurned(false);

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

  // The label's first dot at offset 0: the middle of the slack on a
  // Bluetooth printer, the roll's own place on a Brother (Spec V75; the two
  // agree for DK-11209, dot 12).
  const lv_coord_t x = labelPrinterContentX(printer);
  const int16_t offset = labelPrinterOffset(printer);
  const lv_coord_t centre = x - offset;

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
    char stock[LABEL_LINE_LEN], info[LABEL_LINE_LEN];
    labelMediaShort(printer.model, printer.media_width_mm, printer.media_length_mm, stock,
                    sizeof(stock));
    snprintf(info, sizeof(info), "%s  %s  %s %s%d mm  %s",
             labelPrinterProfile(printer.model).name, stock, T(STR_W_P_CAL_OFFSET),
             off_mm > 0 ? "+" : "", off_mm, dash ? dash + 1 : FW_VERSION);
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
