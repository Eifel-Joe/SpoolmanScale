"""Compile the production TSPL job rules of the TYPONOS PM240 (Spec 5.3 AC a10, V74) and check the bytes a 50 x 30 mm label becomes.

The job is read off AreYouThereAndi2's capture of the vendor app: SIZE, GAP, DIRECTION, DENSITY, CLS, one BITMAP with the rows, PRINT. TSPL's BITMAP takes a 0 bit as a black dot, the raster a 1; the head prints 384 dots (48 mm at 203 dpi), and a 400 dot row of a 50 mm label is cut to its middle 384, the part under the head."""
import subprocess
import tempfile
from pathlib import Path

import klipper_src as ks

root = Path(__file__).parents[1]
ks.assert_ci_step("test_tspl_job.py")

source = r'''
#include "services/tspl_rules.h"
#include <cassert>
#include <cstring>

// The numbers of the capture, spelled out here on purpose.
static_assert(TSPL_HEAD_DOTS == 384, "48 mm at 203 dpi");
static_assert(TSPL_GAP_MM == 6, "6 mm between labels, as the app sent");
static_assert(TSPL_DENSITY == 10, "heat 10 of 15, as the app sent");

int main() {
  // The head's part of a row: a 50 mm label's 400 dots lose 8 at each side,
  // a row that fits goes whole.
  TsplCut c = tsplCut(400, 50);
  assert(c.row_bytes == 48 && c.skip == 1);
  c = tsplCut(384, 48);
  assert(c.row_bytes == 48 && c.skip == 0);
  c = tsplCut(320, 40);
  assert(c.row_bytes == 40 && c.skip == 0);

  // The text before the rows, for the capture's 50 x 30 label of 240 rows.
  char head[160];
  const char want[] = "SIZE 50.0 mm,30.0 mm\r\nGAP 6.0 mm,0 mm\r\nDIRECTION 0,0\r\n"
                      "DENSITY 10\r\nCLS\r\nBITMAP 0,0,48,240,1,";
  const size_t n = tsplHead(head, sizeof(head), 50, 30, 48, 240);
  assert(n == strlen(want) && memcmp(head, want, n) == 0);
  // No room: nothing, never a cut job.
  assert(tsplHead(head, strlen(want), 50, 30, 48, 240) == 0);
  assert(tsplHead(nullptr, 0, 50, 30, 48, 240) == 0);
  // Another size says its own numbers.
  assert(tsplHead(head, sizeof(head), 40, 30, 48, 240) > 0 && memcmp(head, "SIZE 40.0 mm,30.0 mm\r\n", 22) == 0);

  // One row: the bytes under the head, every bit turned (0 is black in TSPL).
  uint8_t row[50];
  for (int i = 0; i < 50; i++) row[i] = (uint8_t)i;
  row[0] = 0xAA; row[1] = 0xFF; row[48] = 0x0F; row[49] = 0x55;
  uint8_t out[48];
  tsplRow(row, tsplCut(400, 50), out);
  assert(out[0] == 0x00);              // row[1], all black
  assert(out[1] == (uint8_t)~2);
  assert(out[47] == 0xF0);             // row[48]
  // A row that fits goes whole.
  tsplRow(row, tsplCut(384, 48), out);
  assert(out[0] == (uint8_t)~0xAA && out[47] == (uint8_t)~47);

  // The end of the job.
  assert(tsplTailLen() == 11 && memcmp(TSPL_TAIL, "\r\nPRINT 1\r\n", 11) == 0);
  return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "test.cpp"
    cpp.write_text(source)
    binary = Path(tmp) / "test"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"), str(cpp),
                    str(root / "src/services/tspl_rules.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

# The driver builds its job from these rules alone, the cut and the turn of
# the bits included, so the host test covers what goes on the air.
drv = ks.squeeze(ks.body_of("services/tspl_printer.cpp", "tsplPrint"))
for call in ("tsplCut(", "tsplHead(", "tsplRow(", "TSPL_TAIL"):
    assert call in drv, f"src/services/tspl_printer.cpp: tsplPrint() does not use {call.rstrip('(')}"
print("test_tspl_job: ok")
