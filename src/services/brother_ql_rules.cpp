#include "services/brother_ql_rules.h"

#include <string.h>

// DK-11209, 62 x 29 die-cut: the head's 720 pins leave 12 dots of margin on
// each side, 696 are printable across; along the feed 271 of the 341 lines
// (R p.15-16, p.19-20; befunde 2). More DK labels go into this table.
const BrotherQlLabel BROTHER_QL_LABELS[] = {
  {62, 29, 696, 271, 0x0B},
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

size_t brotherQlStart(const BrotherQlLabel& label, uint16_t lines, uint8_t* out, size_t cap) {
  if (!out || cap < BROTHER_QL_START_BYTES) return 0;
  // Befunde 1, steps 2 to 9. The count is checked below, not left to the
  // compiler's zero fill: a byte too few would be sent as a null.
  const uint8_t start[] = {
    0x1B, 0x40,                                  // initialize
    0x1B, 0x69, 0x61, 0x01,                      // raster mode (R p.29)
    // Print information (R p.32): valid flags, media type, width and length in
    // mm, the number of lines in four bytes little endian, first page, 0.
    0x1B, 0x69, 0x7A, 0xCE, label.media_type, label.width_mm, label.length_mm,
    (uint8_t)(lines & 0xFF), (uint8_t)(lines >> 8), 0x00, 0x00,
    0x00, 0x00,
    0x1B, 0x69, 0x4D, 0x40,                      // auto cut (R p.35)
    0x1B, 0x69, 0x41, 0x01,                      // cut after every label (R p.34)
    0x1B, 0x69, 0x4B, 0x08,                      // expanded: cut at end, one colour, normal res. (R p.35)
    0x1B, 0x69, 0x64, 0x00, 0x00,                // margin 0, the only one a die-cut label takes (R p.16)
    0x4D, 0x00,                                  // no compression, said outright (R p.33)
  };
  static_assert(sizeof(start) == BROTHER_QL_START_BYTES, "the start of a job is 38 bytes");
  memcpy(out, start, sizeof(start));
  return sizeof(start);
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

size_t brotherQlJobBytes(uint16_t lines) {
  return size_t(BROTHER_QL_INVALIDATE) + BROTHER_QL_START_BYTES +
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
