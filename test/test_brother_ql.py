"""Compile the production Brother QL job rules and check the bytes a QL-810W/820NWB takes for a 62 x 29 label and for the other rolls (62 mm and 38 mm continuous, 23 x 23 die-cut, v0.8.7-beta.7, Spec V75), the bytes of a QL-700 behind a forwarder, where each roll sits in the 720 dot row and how far its print position moves, the key its position is kept under, the mirrored raster row, the printer's address, how a line held open after the job ends and how the printer's bytes are shown, and which link an update leaves the printer on."""
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
  assert(l && l->dots_w == 696 && l->dots_h == 271 && l->media_type == 0x0B && l->margin_r == 12);
  assert(!brotherQlLabel(50, 30) && !brotherQlLabel(62, 31) && !brotherQlLabel(23, 24));
  assert(brotherQlPrintWidth(62) == 696 && brotherQlPrintWidth(50) == 0);
  // The other rolls (Spec V75), as Brother's references give them: the dots
  // across and the pins right of them (R p.14-20; QL series reference p.6-10;
  // brother_ql labels.py lines 86, 89, 95). 62 and 38 mm are continuous tape,
  // cut where the job ends; 3 mm of feed come before and after the print
  // (R p.18), so a label cut at 30 mm prints 24 of them: 354 - 2 x 35 = 284
  // lines, at 40 mm 472 - 70 = 402. The 23 x 23 square prints 236 x 202
  // (Brother; brother_ql leaves it at 202 x 202).
  const BrotherQlLabel* c62 = brotherQlLabel(62, 30);
  assert(c62 && c62->dots_w == 696 && c62->dots_h == 284 && c62->media_type == 0x0A && c62->margin_r == 12);
  const BrotherQlLabel* c38 = brotherQlLabel(38, 40);
  assert(c38 && c38->dots_w == 413 && c38->dots_h == 402 && c38->media_type == 0x0A && c38->margin_r == 12);
  const BrotherQlLabel* sq = brotherQlLabel(23, 23);
  assert(sq && sq->dots_w == 236 && sq->dots_h == 202 && sq->media_type == 0x0B && sq->margin_r == 42);
  assert(BROTHER_QL_LABEL_COUNT == 4 && &BROTHER_QL_LABELS[0] == l);
  assert(brotherQlPrintWidth(38) == 413 && brotherQlPrintWidth(23) == 236);
  // Init, raster mode, print info (271 lines LE), auto cut, cut each, cut at end, margin 0, no compression.
  const uint8_t want[] = {0x1B,0x40, 0x1B,0x69,0x61,0x01,
    0x1B,0x69,0x7A,0xCE,0x0B,0x3E,0x1D,0x0F,0x01,0x00,0x00,0x00,0x00,
    0x1B,0x69,0x4D,0x40, 0x1B,0x69,0x41,0x01, 0x1B,0x69,0x4B,0x08,
    0x1B,0x69,0x64,0x00,0x00, 0x4D,0x00};
  uint8_t s[64];
  assert(sizeof(want) == BROTHER_QL_START_BYTES);
  assert(brotherQlStart(*l, BQM_QL800, 271, s, sizeof(s)) == sizeof(want) && memcmp(s, want, sizeof(want)) == 0);
  assert(brotherQlStart(*l, BQM_QL800, 271, s, sizeof(want) - 1) == 0);
  brotherQlStart(*l, BQM_QL800, 300, s, sizeof(s));
  assert(s[13] == 0x2C && s[14] == 0x01 && s[15] == 0 && s[16] == 0);
  // Continuous tape: media 0A, length 0 (the lines set it), 35 dots of feed
  // margin (brother_ql conversion.py, R p.28, p.32).
  const uint8_t want_c62[] = {0x1B,0x40, 0x1B,0x69,0x61,0x01,
    0x1B,0x69,0x7A,0xCE,0x0A,0x3E,0x00,0x1C,0x01,0x00,0x00,0x00,0x00,
    0x1B,0x69,0x4D,0x40, 0x1B,0x69,0x41,0x01, 0x1B,0x69,0x4B,0x08,
    0x1B,0x69,0x64,0x23,0x00, 0x4D,0x00};
  assert(brotherQlStart(*c62, BQM_QL800, 284, s, sizeof(s)) == sizeof(want_c62) &&
         memcmp(s, want_c62, sizeof(want_c62)) == 0);
  const uint8_t want_c38[] = {0x1B,0x69,0x7A,0xCE,0x0A,0x26,0x00,0x92,0x01,0x00,0x00,0x00,0x00};
  assert(brotherQlStart(*c38, BQM_QL800, 402, s, sizeof(s)) == BROTHER_QL_START_BYTES &&
         memcmp(s + 6, want_c38, sizeof(want_c38)) == 0 && s[34] == 0x23 && s[35] == 0x00);
  const uint8_t want_sq[] = {0x1B,0x69,0x7A,0xCE,0x0B,0x17,0x17,0xCA,0x00,0x00,0x00,0x00,0x00};
  assert(brotherQlStart(*sq, BQM_QL800, 202, s, sizeof(s)) == BROTHER_QL_START_BYTES &&
         memcmp(s + 6, want_sq, sizeof(want_sq)) == 0 && s[34] == 0x00 && s[35] == 0x00);
  // The QL-700 (only USB, behind a forwarder): what brother_ql sends it -
  // no raster-mode switch and no compression command (models.py line 52,
  // mode_setting and compression False; the QL series reference says both
  // are for other models only, p.4, p.18, p.21), 200 null bytes before.
  static_assert(BROTHER_QL700_INVALIDATE == 200, "the QL-700's null bytes");
  static_assert(BROTHER_QL700_START_BYTES == 32, "38 without ESC i a 01 and 4D 00");
  const uint8_t want_700[] = {0x1B,0x40,
    0x1B,0x69,0x7A,0xCE,0x0B,0x3E,0x1D,0x0F,0x01,0x00,0x00,0x00,0x00,
    0x1B,0x69,0x4D,0x40, 0x1B,0x69,0x41,0x01, 0x1B,0x69,0x4B,0x08,
    0x1B,0x69,0x64,0x00,0x00};
  assert(sizeof(want_700) == BROTHER_QL700_START_BYTES);
  assert(brotherQlStart(*l, BQM_QL700, 271, s, sizeof(s)) == sizeof(want_700) &&
         memcmp(s, want_700, sizeof(want_700)) == 0);
  assert(brotherQlStart(*l, BQM_QL700, 271, s, sizeof(want_700) - 1) == 0);
  assert(brotherQlStart(*c62, BQM_QL700, 284, s, sizeof(s)) == BROTHER_QL700_START_BYTES &&
         s[6] == 0x0A && s[8] == 0x00 && s[29] == 0x23);
  assert(brotherQlInvalidate(BQM_QL800) == 400 && brotherQlInvalidate(BQM_QL700) == 200);
  assert(brotherQlStartBytes(BQM_QL800) == 38 && brotherQlStartBytes(BQM_QL700) == 32);
  assert(brotherQlJobBytes(BQM_QL700, 271) == 200 + 32 + 271 * 93 + 1);
  assert(brotherQlJobBytes(BQM_QL800, 284) == 400 + 38 + 284 * 93 + 1);
  // Where each roll sits in the 720 dot row as the scale draws it (dot 0 on
  // the left; the row goes out mirrored): 720 - printable - pins on the right.
  // Brother's pin tables: 12/696/12, 295/413/12, 442/236/42 (R p.19-20).
  assert(brotherQlContentX(*l) == 12 && brotherQlContentX(*c62) == 12);
  assert(brotherQlContentX(*c38) == 295 && brotherQlContentX(*sq) == 442);
  // The print position: 5 mm (59 dots) either way from there, as far as the
  // row goes (Ziopase: DK-11221 came out 3 to 4 mm off on a QL-700). On the
  // 62 mm rolls that is the 12 dots there are, as before.
  int16_t lo = 0, hi = 0;
  brotherQlOffsetRange(*l, &lo, &hi);  assert(lo == -12 && hi == 12);
  brotherQlOffsetRange(*c62, &lo, &hi); assert(lo == -12 && hi == 12);
  brotherQlOffsetRange(*c38, &lo, &hi); assert(lo == -59 && hi == 12);
  brotherQlOffsetRange(*sq, &lo, &hi);  assert(lo == -59 && hi == 42);
  // Each roll keeps its own: 62 x 29 under the key it always had, the others
  // under keys of their own, 15 characters at most.
  char key[16];
  assert(brotherQlOffsetKey(*l, key, sizeof(key)) && strcmp(key, "px_brother") == 0);
  assert(brotherQlOffsetKey(*c62, key, sizeof(key)) && strcmp(key, "px_bq_6230") == 0);
  assert(brotherQlOffsetKey(*c38, key, sizeof(key)) && strcmp(key, "px_bq_3840") == 0);
  assert(brotherQlOffsetKey(*sq, key, sizeof(key)) && strcmp(key, "px_bq_2323") == 0);
  assert(!brotherQlOffsetKey(*sq, key, 8));
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
  assert(brotherQlJobBytes(BQM_QL800, 271) == 25642);
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
