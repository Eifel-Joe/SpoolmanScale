"""Compile the production Brother QL job rules and check the bytes a QL-810W/820NWB takes for a 62 x 29 label, the mirrored raster row, the printable area, the printer's address, how a line held open after the job ends and how the printer's bytes are shown, and which link an update leaves the printer on."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/brother_ql_rules.h"
#include "services/label_printer.h"
#include <cassert>
#include <cstring>
#include <initializer_list>

// The numbers of the protocol, spelled out here on purpose: the production
// defines must agree with this text, not with themselves. The raw socket is
// port 9100; the job starts with 400 null bytes to flush the printer's buffer;
// the head has 720 pins, 90 bytes a row, and a row on the wire is 67 00 5A and
// those 90 bytes; the start of a job is 38 bytes; 0x1A prints and cuts.
static_assert(BROTHER_QL_PORT == 9100, "the raw socket");
static_assert(BROTHER_QL_INVALIDATE == 400, "null bytes before the job");
static_assert(BROTHER_QL_HEAD_DOTS == 720, "pins of the head");
static_assert(BROTHER_QL_ROW_BYTES == 90, "720 pins, 8 to a byte");
static_assert(BROTHER_QL_LINE_BYTES == 93, "67 00 5A and one row");
static_assert(BROTHER_QL_START_BYTES == 38, "the bytes before the first row");
static_assert(BROTHER_QL_PRINT == 0x1A, "print with feeding");
// After the last byte the line stays open for 10 s at most (Spec 5.3 L1), and
// the loop looks at it on every pass instead of waiting for it.
static_assert(BROTHER_QL_HOLD_MS == 10000, "the printer's 10 s");

int main() {
  // DK-11209: 62 x 29 die-cut, 696 x 271 dots at 300 dpi (befunde 2).
  const BrotherQlLabel* l = brotherQlLabel(62, 29);
  assert(l && l->dots_w == 696 && l->dots_h == 271 && l->media_type == 0x0B);
  assert(!brotherQlLabel(62, 30) && !brotherQlLabel(50, 30));
  assert(brotherQlPrintWidth(62) == 696 && brotherQlPrintWidth(50) == 0);
  // Init, raster mode, print info (271 lines LE), auto cut, cut each, cut at end, margin 0, no compression.
  const uint8_t want[] = {0x1B,0x40, 0x1B,0x69,0x61,0x01,
    0x1B,0x69,0x7A,0xCE,0x0B,0x3E,0x1D,0x0F,0x01,0x00,0x00,0x00,0x00,
    0x1B,0x69,0x4D,0x40, 0x1B,0x69,0x41,0x01, 0x1B,0x69,0x4B,0x08,
    0x1B,0x69,0x64,0x00,0x00, 0x4D,0x00};
  uint8_t s[64];
  assert(sizeof(want) == BROTHER_QL_START_BYTES);
  assert(brotherQlStart(*l, 271, s, sizeof(s)) == sizeof(want) && memcmp(s, want, sizeof(want)) == 0);
  assert(brotherQlStart(*l, 271, s, sizeof(want) - 1) == 0);
  brotherQlStart(*l, 300, s, sizeof(s));
  assert(s[13] == 0x2C && s[14] == 0x01 && s[15] == 0 && s[16] == 0);
  // A row: the raster's dot 0 lands on the printer's last pin.
  uint8_t row[90] = {}, out[93];
  row[0] = 0x80;
  brotherQlRow(row, out);
  assert(out[0] == 0x67 && out[1] == 0x00 && out[2] == 0x5A && out[92] == 0x01);
  for (int i = 3; i < 92; i++) assert(out[i] == 0);
  memset(row, 0, sizeof(row)); row[89] = 0x01; row[1] = 0x40;   // dots 719 and 9
  brotherQlRow(row, out);
  assert(out[3] == 0x80 && out[91] == 0x02);
  // The label at dot 12 of the 720 dot row, 696 wide: 12 white dots each side.
  memset(row, 0xFF, sizeof(row)); row[0] = 0x00; row[1] = 0x0F; row[88] = 0xF0; row[89] = 0x00;
  brotherQlRow(row, out);
  assert(out[3] == 0x00 && out[4] == 0x0F && out[91] == 0xF0 && out[92] == 0x00 && out[50] == 0xFF);
  assert(brotherQlJobBytes(271) == 25642);
  // The printer's address.
  assert(brotherQlHostValid("192.168.10.50") && brotherQlHostValid("10.0.0.1"));
  for (const char* bad : {"", "192.168.10", "192.168.10.256", "192.168.10.5.1", "192.168.010.5",
                          " 192.168.10.5", "192.168.10.5 ", "a.b.c.d", "192.168..5", "0.1.2.3", "brother.local"})
    assert(!brotherQlHostValid(bad));
  assert(!brotherQlHostValid(nullptr));
  // The choice after an update: what Bluetooth was, unless WLAN was stored.
  assert(labelPrinterLinkFrom(-1, false) == LP_LINK_OFF && labelPrinterLinkFrom(-1, true) == LP_LINK_BLE);
  assert(labelPrinterLinkFrom(LP_LINK_WLAN, false) == LP_LINK_WLAN && labelPrinterLinkFrom(LP_LINK_WLAN, true) == LP_LINK_WLAN);
  assert(labelPrinterLinkFrom(LP_LINK_OFF, true) == LP_LINK_BLE && labelPrinterLinkFrom(LP_LINK_BLE, false) == LP_LINK_OFF);
  assert(labelPrinterLinkFrom(7, false) == LP_LINK_OFF);
  // One look at the held line: a recv() that does not wait. Bytes are the
  // printer's answer and are shown, whenever they come; 0 is the printer's
  // close, its normal end; nothing yet waits until the 10 s are over; any
  // other failure is the line broken.
  assert(brotherQlHoldVerdict(-1, true, 0) == BQH_WAIT);
  assert(brotherQlHoldVerdict(-1, true, 9999) == BQH_WAIT);
  assert(brotherQlHoldVerdict(-1, true, 10000) == BQH_TIMEOUT);
  assert(brotherQlHoldVerdict(-1, true, 60000) == BQH_TIMEOUT);
  assert(brotherQlHoldVerdict(0, false, 300) == BQH_CLOSED);
  assert(brotherQlHoldVerdict(0, true, 300) == BQH_CLOSED);
  assert(brotherQlHoldVerdict(0, false, 12000) == BQH_CLOSED);
  assert(brotherQlHoldVerdict(32, false, 300) == BQH_DATA);
  assert(brotherQlHoldVerdict(1, true, 12000) == BQH_DATA);
  assert(brotherQlHoldVerdict(-1, false, 300) == BQH_ABORT);
  assert(brotherQlHoldVerdict(-1, false, 12000) == BQH_ABORT);
  // What the printer sent, for the card and the log: two hex digits a byte,
  // a blank between them, whole bytes only when the room runs out.
  const uint8_t got[] = {0x80, 0x20, 0x42, 0x34, 0x0A};
  char hex[16];
  assert(brotherQlBytesHex(got, 3, hex, sizeof(hex)) == 8 && strcmp(hex, "80 20 42") == 0);
  assert(brotherQlBytesHex(got, 5, hex, sizeof(hex)) == 14 && strcmp(hex, "80 20 42 34 0A") == 0);
  assert(brotherQlBytesHex(got, 5, hex, 12) == 11 && strcmp(hex, "80 20 42 34") == 0);
  assert(brotherQlBytesHex(got, 5, hex, 3) == 2 && strcmp(hex, "80") == 0);
  assert(brotherQlBytesHex(got, 5, hex, 2) == 0 && hex[0] == '\0');
  assert(brotherQlBytesHex(got, 0, hex, sizeof(hex)) == 0 && hex[0] == '\0');
  assert(brotherQlBytesHex(nullptr, 5, hex, sizeof(hex)) == 0 && hex[0] == '\0');
  assert(brotherQlBytesHex(got, 5, nullptr, 0) == 0);
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "brother_ql_rules.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("brother ql rules: ok")
