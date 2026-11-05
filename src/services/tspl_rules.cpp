#include "services/tspl_rules.h"

#include <stdio.h>
#include <string.h>

const char TSPL_TAIL[] = "\r\nPRINT 1\r\n";

size_t tsplTailLen() { return sizeof(TSPL_TAIL) - 1; }

TsplCut tsplCut(uint16_t raster_width, uint16_t raster_row_bytes) {
  const uint16_t width = raster_width > TSPL_HEAD_DOTS ? TSPL_HEAD_DOTS : raster_width;
  TsplCut c;
  c.row_bytes = width / 8;
  c.skip = raster_row_bytes > c.row_bytes ? (raster_row_bytes - c.row_bytes) / 2 : 0;
  return c;
}

size_t tsplHead(char* out, size_t cap, const TsplLabel& label) {
  if (!out || !cap) return 0;
  const int n = snprintf(out, cap,
                         "SIZE %u.0 mm,%u.0 mm\r\nGAP %u.0 mm,0 mm\r\nDIRECTION 0,0\r\n"
                         "DENSITY %u\r\nCLS\r\nBITMAP 0,0,%u,%u,1,",
                         (unsigned)label.media_w_mm, (unsigned)label.media_l_mm, (unsigned)TSPL_GAP_MM,
                         (unsigned)TSPL_DENSITY, (unsigned)label.row_bytes, (unsigned)label.rows);
  return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

void tsplRow(const uint8_t* raster_row, const TsplCut& cut, uint8_t* out) {
  const uint8_t* src = raster_row + cut.skip;
  for (uint16_t x = 0; x < cut.row_bytes; ++x) out[x] = (uint8_t)~src[x];
}
