#include "services/brother_ql_rules.h"

#include <stdio.h>
#include <string.h>

// DK-11209, 62 x 29 die-cut: the head's 720 pins leave 12 dots of margin on
// each side, 696 are printable across; along the feed 271 of the 341 lines
// (R p.15-16, p.19-20; befunde 2). More DK labels go into this table.
// The other three (Spec V75, f08), from Brother's print areas and pin
// tables (R p.14-15, p.19-20; Q p.6-10), which bql follows except for the
// square:
//   DK-22205, 62 mm continuous, cut at 30 mm: 696 across, pins 12 on the
//     right; 354 lines for 30 mm less 2 x 35 of feed = 284.
//   DK-22225, 38 mm continuous, cut at 40 mm: 413 across, pins 295 left
//     and 12 right; 472 - 70 = 402 lines.
//   DK-11221, 23 x 23 die-cut: 236 x 202 (bql and f08 take 202 x 202,
//     Brother prints 236 across), pins 442 left and 42 right.
const BrotherQlLabel BROTHER_QL_LABELS[] = {
  {62, 29, 696, 271, BROTHER_QL_DIE_CUT,    12},
  {62, 30, 696, 284, BROTHER_QL_CONTINUOUS, 12},
  {38, 40, 413, 402, BROTHER_QL_CONTINUOUS, 12},
  {23, 23, 236, 202, BROTHER_QL_DIE_CUT,    42},
};
const int BROTHER_QL_LABEL_COUNT = sizeof(BROTHER_QL_LABELS) / sizeof(BROTHER_QL_LABELS[0]);

const BrotherQlLabel* brotherQlLabel(uint8_t width_mm, uint8_t length_mm) {
  for (int i = 0; i < BROTHER_QL_LABEL_COUNT; i++)
    if (BROTHER_QL_LABELS[i].width_mm == width_mm && BROTHER_QL_LABELS[i].length_mm == length_mm)
      return &BROTHER_QL_LABELS[i];
  return nullptr;
}

uint16_t brotherQlPrintWidth(uint8_t width_mm) {
  for (int i = 0; i < BROTHER_QL_LABEL_COUNT; i++)
    if (BROTHER_QL_LABELS[i].width_mm == width_mm) return BROTHER_QL_LABELS[i].dots_w;
  return 0;
}

size_t brotherQlInvalidate(BrotherQlModel model) {
  return model == BQM_QL700 ? BROTHER_QL700_INVALIDATE : BROTHER_QL_INVALIDATE;
}

size_t brotherQlStartBytes(BrotherQlModel model) {
  return model == BQM_QL700 ? BROTHER_QL700_START_BYTES : BROTHER_QL_START_BYTES;
}

size_t brotherQlStart(const BrotherQlLabel& label, BrotherQlModel model, uint16_t lines,
                      uint8_t* out, size_t cap) {
  const size_t want = brotherQlStartBytes(model);
  if (!out || cap < want) return 0;
  const bool continuous = label.media_type == BROTHER_QL_CONTINUOUS;
  // Continuous tape: no length of its own, the lines set it (R p.25, "fixed
  // at 00h"), and the feed margin before and after; die-cut: margin 0.
  const uint8_t length = continuous ? 0 : label.length_mm;
  const uint8_t margin = continuous ? BROTHER_QL_FEED_DOTS : 0;
  const bool ql800 = model != BQM_QL700;
  // Befunde 1, steps 2 to 9. The count is checked below, not left to the
  // compiler's zero fill: a byte too few would be sent as a null.
  const uint8_t start[] = {
    0x1B, 0x40,                                  // initialize
    0x1B, 0x69, 0x61, 0x01,                      // raster mode (R p.29); not the QL-700's
    // Print information (R p.32): valid flags, media type, width and length in
    // mm, the number of lines in four bytes little endian, first page, 0.
    0x1B, 0x69, 0x7A, 0xCE, label.media_type, label.width_mm, length,
    (uint8_t)(lines & 0xFF), (uint8_t)(lines >> 8), 0x00, 0x00,
    0x00, 0x00,
    0x1B, 0x69, 0x4D, 0x40,                      // auto cut (R p.35)
    0x1B, 0x69, 0x41, 0x01,                      // cut after every label (R p.34)
    0x1B, 0x69, 0x4B, 0x08,                      // expanded: cut at end, one colour, normal res. (R p.35)
    0x1B, 0x69, 0x64, margin, 0x00,              // margin: 0 die-cut (R p.16), 35 continuous (R p.28)
    0x4D, 0x00,                                  // no compression, said outright (R p.33); not the QL-700's
  };
  static_assert(sizeof(start) == BROTHER_QL_START_BYTES, "the start of a job is 38 bytes");
  if (ql800) {
    memcpy(out, start, sizeof(start));
    return sizeof(start);
  }
  // The QL-700: the same without the raster-mode switch (bytes 2 to 5) and
  // the compression command (the last two), as bql writes it (models.py
  // line 52).
  static_assert(sizeof(start) - 4 - 2 == BROTHER_QL700_START_BYTES, "the QL-700's start");
  memcpy(out, start, 2);
  memcpy(out + 2, start + 6, sizeof(start) - 6 - 2);
  return BROTHER_QL700_START_BYTES;
}

uint16_t brotherQlContentX(const BrotherQlLabel& label) {
  const int x = BROTHER_QL_HEAD_DOTS - (int)label.dots_w - (int)label.margin_r;
  return x > 0 ? (uint16_t)x : 0;
}

void brotherQlOffsetRange(const BrotherQlLabel& label, int16_t* min, int16_t* max) {
  // Room to the left: the dots before the print area; to the right: the
  // pins after it. Each at most BROTHER_QL_OFFSET_DOTS.
  const int left = brotherQlContentX(label);
  const int right = label.margin_r;
  if (min) *min = (int16_t)-(left < BROTHER_QL_OFFSET_DOTS ? left : BROTHER_QL_OFFSET_DOTS);
  if (max) *max = (int16_t)(right < BROTHER_QL_OFFSET_DOTS ? right : BROTHER_QL_OFFSET_DOTS);
}

bool brotherQlOffsetKey(const BrotherQlLabel& label, char* out, size_t n) {
  if (!out || !n) return false;
  // DK-11209's position has stood under this key since v0.8.5-beta.6, and a
  // scale keeps it through the update.
  const int len = (label.width_mm == 62 && label.length_mm == 29)
      ? snprintf(out, n, "px_brother")
      : snprintf(out, n, "px_bq_%02u%02u", (unsigned)label.width_mm, (unsigned)label.length_mm);
  if (len < 0 || (size_t)len >= n) {
    out[0] = '\0';
    return false;
  }
  return true;
}

// The bits of a byte in the opposite order: halves, pairs, then neighbours.
static uint8_t reverseBits(uint8_t b) {
  b = (uint8_t)(((b & 0xF0) >> 4) | ((b & 0x0F) << 4));
  b = (uint8_t)(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
  b = (uint8_t)(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
  return b;
}

void brotherQlRow(const uint8_t* row, uint8_t* out) {
  // 67 00 5A: raster graphics transfer, 90 data bytes follow (R p.30).
  out[0] = 0x67;
  out[1] = 0x00;
  out[2] = 0x5A;
  // The raster's dot 0 is on the left; the printer's dot 0 is the pin on the
  // right of the print (R p.19), so the last byte of the row goes first and
  // every byte is turned around (befunde 2).
  for (int i = 0; i < BROTHER_QL_ROW_BYTES; i++)
    out[3 + i] = reverseBits(row[BROTHER_QL_ROW_BYTES - 1 - i]);
}

size_t brotherQlJobBytes(BrotherQlModel model, uint16_t lines) {
  return brotherQlInvalidate(model) + brotherQlStartBytes(model) +
         size_t(lines) * BROTHER_QL_LINE_BYTES + 1;   // 1: the print byte
}

// Digits of one number at p, moved past them. False for none, more than
// three, a leading zero or a value over 255.
static bool hostNumber(const char** p, int* value) {
  const char* s = *p;
  int digits = 0, v = 0;
  while (*s >= '0' && *s <= '9') {
    if (++digits > 3) return false;
    v = v * 10 + (*s - '0');
    s++;
  }
  if (digits == 0 || v > 255 || (digits > 1 && **p == '0')) return false;
  *p = s;
  *value = v;
  return true;
}

bool brotherQlHostValid(const char* host) {
  if (!host) return false;
  const char* p = host;
  for (int part = 0; part < 4; part++) {
    int value = 0;
    if (!hostNumber(&p, &value)) return false;
    if (part == 0 && value == 0) return false;   // 0.x.x.x is no host
    if (part < 3) {
      if (*p != '.') return false;
      p++;
    }
  }
  return *p == '\0';
}

BrotherQlHold brotherQlHoldVerdict(int got, bool would_block, uint32_t held_ms) {
  // Bytes first: whatever the printer says is worth showing, also late.
  if (got > 0) return BQH_DATA;
  if (got == 0) return BQH_CLOSED;
  if (!would_block) return BQH_ABORT;
  return held_ms >= BROTHER_QL_HOLD_MS ? BQH_TIMEOUT : BQH_WAIT;
}

size_t brotherQlBytesHex(const uint8_t* bytes, size_t n, char* out, size_t out_len) {
  if (!out || out_len == 0) return 0;
  out[0] = '\0';
  if (!bytes) return 0;
  static const char DIGITS[] = "0123456789ABCDEF";
  size_t o = 0;
  for (size_t i = 0; i < n; i++) {
    // Two digits, a blank before every byte but the first, and the end.
    if (o + (i ? 3 : 2) + 1 > out_len) break;
    if (i) out[o++] = ' ';
    out[o++] = DIGITS[bytes[i] >> 4];
    out[o++] = DIGITS[bytes[i] & 0x0F];
  }
  out[o] = '\0';
  return o;
}
