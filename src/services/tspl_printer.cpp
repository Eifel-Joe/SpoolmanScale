#include "services/tspl_printer.h"

#include <esp_heap_caps.h>
#include <stdio.h>
#include <stdlib.h>

#include "hardware/sd_logger.h"
#include "services/tspl_rules.h"

// TSPL ends a job when the last line is in, but the printer prints from its
// buffer and a link closed right after the data cost the M220 its label. No
// status characteristic is known for the PM240, so the session stays for a
// while instead: a 30 mm label is through in about a second.
static const BleDone TSPL_HOLD = { 0, nullptr, 0, 3000 };

BleWriteResult tsplPrint(const char* address, const LabelRaster& image,
                         uint16_t media_w_mm, uint16_t media_l_mm, BleProgressFn progress) {
  if (!labelRasterValid(image)) return BLE_WRITE_FAILED;
  // The head's part of each row: all of it when the row fits, else the middle.
  const TsplCut cut = tsplCut(image.width, image.row_bytes);
  const size_t len = size_t(cut.row_bytes) * image.height;
  uint8_t* bits = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
  if (!bits) bits = (uint8_t*)malloc(len);
  if (!bits) return BLE_WRITE_INIT_FAILED;
  for (uint16_t y = 0; y < image.height; ++y)
    tsplRow(image.pixels + size_t(y) * image.row_bytes, cut, bits + size_t(y) * cut.row_bytes);

  char head[160];
  const size_t n = tsplHead(head, sizeof(head), media_w_mm, media_l_mm, cut.row_bytes, image.height);
  if (!n) { free(bits); return BLE_WRITE_FAILED; }
  logSDf("TSPL: %ux%u mm, %u bytes x %u rows (raster %u wide) to %s",
         (unsigned)media_w_mm, (unsigned)media_l_mm, (unsigned)cut.row_bytes, (unsigned)image.height,
         (unsigned)image.width, address);

  const BleBlock blocks[] = {
    { (const uint8_t*)head, n },
    { bits, len },
    { (const uint8_t*)TSPL_TAIL, tsplTailLen() },
  };
  const BleWriteResult r = bleWriteBlocks(address, TSPL_SERVICE_UUID, TSPL_WRITE_UUID, blocks,
                                          sizeof(blocks) / sizeof(blocks[0]), progress, &TSPL_HOLD);
  free(bits);
  return r;
}
