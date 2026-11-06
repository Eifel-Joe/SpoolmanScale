"""Compile the production rules of the Brother's status over the network (v0.8.7-beta.8, Spec V78) and check them: the SNMP request a QL-810W/820NWB answers with its 32 byte status block, the reading of that answer (the bytes are those Joerg's QL-820NWBc sent on 06.11.2026, with DK-11209 loaded), every answer that is not one, and the verdict of the roll in the printer against the roll set at the scale."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/brother_ql_status.h"
#include <cassert>
#include <cstring>
#include <initializer_list>
#include <vector>

// The numbers of the way, spelled out here on purpose: SNMP on UDP port 161,
// the status block is 32 bytes (R p.23), two tries of half a second each,
// so a printer that does not answer costs the print at most 1 s (Spec V78).
static_assert(BROTHER_QL_SNMP_PORT == 161, "SNMP");
static_assert(BROTHER_QL_STATUS_BYTES == 32, "the status block");
static_assert(BROTHER_QL_STATUS_TRIES * BROTHER_QL_STATUS_WAIT_MS == 1000, "1 s at most");

// What Joerg's QL-820NWBc was sent and answered on 06.11.2026 10:19:19
// (docs/recherche/2026-11-06-v087-beta8/statusanfrage/snmp-roh.txt): an
// SNMP v1 GET, community public, request-id 1, of Brother's status OID
// 1.3.6.1.4.1.2435.3.3.9.1.6.1.0 (Brother's developer FAQ), and the
// GetResponse with the block: 62 mm, die-cut (0B), 29 mm, no errors.
static const uint8_t REQUEST_1[] = {
  0x30,0x2c,0x02,0x01,0x00,0x04,0x06,0x70,0x75,0x62,0x6c,0x69,0x63,0xa0,0x1f,0x02,
  0x01,0x01,0x02,0x01,0x00,0x02,0x01,0x00,0x30,0x14,0x30,0x12,0x06,0x0e,0x2b,0x06,
  0x01,0x04,0x01,0x93,0x03,0x03,0x03,0x09,0x01,0x06,0x01,0x00,0x05,0x00};
static const uint8_t ANSWER_1[] = {
  0x30,0x4c,0x02,0x01,0x00,0x04,0x06,0x70,0x75,0x62,0x6c,0x69,0x63,0xa2,0x3f,0x02,
  0x01,0x01,0x02,0x01,0x00,0x02,0x01,0x00,0x30,0x34,0x30,0x32,0x06,0x0e,0x2b,0x06,
  0x01,0x04,0x01,0x93,0x03,0x03,0x03,0x09,0x01,0x06,0x01,0x00,0x04,0x20,
  0x80,0x20,0x42,0x34,0x41,0x30,0x04,0x00,0x00,0x00,0x3e,0x0b,0x00,0x00,0x03,0x00,
  0x00,0x1d,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00};
// Where the block starts in ANSWER_1, to change single bytes of it.
static const size_t BLOCK = sizeof(ANSWER_1) - 32;

static std::vector<uint8_t> answer() { return std::vector<uint8_t>(ANSWER_1, ANSWER_1 + sizeof(ANSWER_1)); }

static bool parse(const std::vector<uint8_t>& a, uint16_t id, BrotherQlStatus* s) {
  return brotherQlStatusParse(a.data(), a.size(), id, s);
}

// The block with roll bytes and error bytes of our choosing, as if read.
static BrotherQlStatus seen(uint8_t width, uint8_t type, uint8_t length,
                            uint8_t err1 = 0, uint8_t err2 = 0) {
  std::vector<uint8_t> a = answer();
  a[BLOCK + 8] = err1; a[BLOCK + 9] = err2;
  a[BLOCK + 10] = width; a[BLOCK + 11] = type; a[BLOCK + 17] = length;
  BrotherQlStatus s{};
  assert(parse(a, 1, &s));
  return s;
}

int main() {
  // The request, byte for byte the one the printer answered.
  uint8_t req[BROTHER_QL_SNMP_REQUEST_MAX];
  assert(brotherQlStatusRequest(1, req, sizeof(req)) == sizeof(REQUEST_1));
  assert(memcmp(req, REQUEST_1, sizeof(REQUEST_1)) == 0);
  // Another number changes only its own bytes; a number of 128 and more
  // takes a second byte (BER integers are signed), the lengths follow.
  assert(brotherQlStatusRequest(0x7f, req, sizeof(req)) == sizeof(REQUEST_1) && req[17] == 0x7f);
  const size_t n200 = brotherQlStatusRequest(200, req, sizeof(req));
  assert(n200 == sizeof(REQUEST_1) + 1 && req[1] == 0x2d && req[14] == 0x20 &&
         req[16] == 0x02 && req[17] == 0x00 && req[18] == 0xc8);
  assert(brotherQlStatusRequest(0xffff, req, sizeof(req)) == sizeof(REQUEST_1) + 2);
  // Too small a buffer: nothing written.
  assert(brotherQlStatusRequest(1, req, sizeof(REQUEST_1) - 1) == 0);
  assert(brotherQlStatusRequest(1, nullptr, 64) == 0);
  static_assert(BROTHER_QL_SNMP_REQUEST_MAX >= sizeof(REQUEST_1) + 2, "room for any number");

  // Joerg's answer: 62 x 29 die-cut, nothing wrong, model code QL-820NWB.
  BrotherQlStatus s{};
  assert(parse(answer(), 1, &s));
  assert(s.width_mm == 62 && s.length_mm == 29 && s.media == BQS_DIE_CUT);
  assert(s.err1 == 0 && s.err2 == 0 && s.model == 0x41);

  // Not an answer to this request, or not the block: false, nothing kept.
  {
    BrotherQlStatus x{};
    assert(!parse(answer(), 2, &x));                      // another request's
    for (size_t cut : {size_t(0), size_t(1), size_t(10), size_t(45), sizeof(ANSWER_1) - 1}) {
      std::vector<uint8_t> a = answer(); a.resize(cut);
      assert(!parse(a, 1, &x));                           // cut short
    }
    std::vector<uint8_t> a = answer(); a.push_back(0x00);
    assert(!parse(a, 1, &x));                             // more than the message says
    a = answer(); a[4] = 0x01;
    assert(!parse(a, 1, &x));                             // SNMP v2c, not what was asked
    a = answer(); a[12] = 'C';
    assert(!parse(a, 1, &x));                             // another community
    a = answer(); a[13] = 0xa0;
    assert(!parse(a, 1, &x));                             // a request, not a response
    a = answer(); a[20] = 0x02;
    assert(!parse(a, 1, &x));                             // error-status noSuchName
    a = answer(); a[40] = 0x05;
    assert(!parse(a, 1, &x));                             // another OID
    a = answer(); a[BLOCK] = 0x81;
    assert(!parse(a, 1, &x));                             // not the block's head mark
    a = answer(); a[BLOCK + 2] = 'X';
    assert(!parse(a, 1, &x));
    a = answer(); a[BLOCK + 3] = '3';
    assert(!parse(a, 1, &x));                             // another series
    // noSuchObject in the place of the value, as a printer without the OID
    // answers to SNMP v2c and an agent might to v1.
    a = answer(); a.resize(BLOCK - 2); a.push_back(0x80); a.push_back(0x00);
    a[1] = (uint8_t)(a.size() - 2); a[14] = (uint8_t)(a.size() - 15);
    a[25] = (uint8_t)(a.size() - 26); a[27] = (uint8_t)(a.size() - 28);
    assert(!parse(a, 1, &x));
    // 31 bytes in the string: not the block.
    a = answer(); a.pop_back(); a[BLOCK - 1] = 31;
    a[1] -= 1; a[14] -= 1; a[25] -= 1; a[27] -= 1;
    assert(!parse(a, 1, &x));
    assert(!brotherQlStatusParse(nullptr, 10, 1, &x));
    assert(!brotherQlStatusParse(ANSWER_1, sizeof(ANSWER_1), 1, nullptr));
    // A length in the long form is read as such (here 0x81 0x4c).
    a = answer(); a.insert(a.begin() + 1, 0x81);
    BrotherQlStatus y{};
    assert(parse(a, 1, &y) && y.width_mm == 62);
    // The answer to a request number of two bytes.
    const size_t n = brotherQlStatusRequest(200, req, sizeof(req));
    (void)n;
    a = answer(); a.insert(a.begin() + 17, 0x00); a[16] = 0x02; a[18] = 0xc8;
    a[1] += 1; a[14] += 1;
    assert(parse(a, 200, &y) && !parse(a, 1, &y));
  }

  // The media type byte as both of Brother's tables write it: 0A/0B in the
  // print information and in brother_ql's reader (and from Joerg's printer),
  // 4A/4B in the status table of R p.26. 00 is no roll; anything else is a
  // type the scale cannot judge.
  assert(seen(62, 0x0A, 0).media == BQS_CONTINUOUS && seen(62, 0x4A, 0).media == BQS_CONTINUOUS);
  assert(seen(62, 0x0B, 29).media == BQS_DIE_CUT && seen(62, 0x4B, 29).media == BQS_DIE_CUT);
  assert(seen(0, 0x00, 0).media == BQS_NONE);
  assert(seen(62, 0x11, 0).media == BQS_OTHER);

  // The rolls the scale has, each against each roll in the printer.
  const BrotherQlLabel& r6229 = *brotherQlLabel(62, 29);
  const BrotherQlLabel& r62c  = *brotherQlLabel(62, 0);
  const BrotherQlLabel& r38c  = *brotherQlLabel(38, 0);
  const BrotherQlLabel& r2323 = *brotherQlLabel(23, 23);
  const BrotherQlLabel* set[] = {&r6229, &r62c, &r38c, &r2323};
  const BrotherQlStatus in[] = {seen(62, 0x0B, 29), seen(62, 0x0A, 0), seen(38, 0x4A, 0), seen(23, 0x4B, 23)};
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      assert(brotherQlRollVerdict(in[j], *set[i]) == (i == j ? BQV_OK : BQV_OTHER));
  // The table's entry for what is in the printer, for "switch to".
  assert(brotherQlStatusLabel(in[0]) == &r6229 && brotherQlStatusLabel(in[1]) == &r62c);
  assert(brotherQlStatusLabel(in[2]) == &r38c && brotherQlStatusLabel(in[3]) == &r2323);
  // Rolls the scale does not have: 29 x 90 die-cut, 29 mm continuous, a
  // die-cut 62 x 100, and 62 wide die-cut with the length of nothing known.
  for (const BrotherQlStatus& u : {seen(29, 0x0B, 90), seen(29, 0x0A, 0), seen(62, 0x0B, 100), seen(12, 0x4A, 0)}) {
    assert(brotherQlRollVerdict(u, r6229) == BQV_UNKNOWN);
    assert(brotherQlRollVerdict(u, r62c) == BQV_UNKNOWN);
    assert(brotherQlStatusLabel(u) == nullptr);
  }
  // A continuous roll's length byte is 0 by the table; one that says more
  // is still that tape.
  assert(brotherQlRollVerdict(seen(62, 0x0A, 5), r62c) == BQV_OK);
  // No roll: type 00, width 0, or the "no media" bit.
  assert(brotherQlRollVerdict(seen(0, 0x00, 0), r6229) == BQV_NO_ROLL);
  assert(brotherQlRollVerdict(seen(62, 0x00, 0), r6229) == BQV_NO_ROLL);
  assert(brotherQlRollVerdict(seen(0, 0x0B, 29), r6229) == BQV_NO_ROLL);
  assert(brotherQlRollVerdict(seen(62, 0x0B, 29, 0x01), r6229) == BQV_NO_ROLL);
  // A type the scale cannot judge: printed as before, unless an error says otherwise.
  assert(brotherQlRollVerdict(seen(62, 0x11, 0), r6229) == BQV_UNCHECKED);
  assert(brotherQlRollVerdict(seen(62, 0x11, 0, 0, 0x80), r6229) == BQV_ERROR);

  // The error bits (R p.24). Cover open comes first: the roll is read
  // again once it is shut. Then no roll, then the roll at its end.
  assert(brotherQlRollVerdict(seen(62, 0x0B, 29, 0, 0x10), r6229) == BQV_COVER);
  assert(brotherQlRollVerdict(seen(62, 0x0A, 0, 0, 0x10), r6229) == BQV_COVER);
  assert(brotherQlRollVerdict(seen(0, 0x00, 0, 0x01, 0x10), r6229) == BQV_COVER);
  assert(brotherQlRollVerdict(seen(62, 0x0B, 29, 0x02), r6229) == BQV_ROLL_END);
  assert(brotherQlRollVerdict(seen(62, 0x0B, 29, 0, 0x40), r6229) == BQV_ROLL_END);
  assert(brotherQlRollVerdict(seen(62, 0x0A, 0, 0x02), r6229) == BQV_ROLL_END);
  // Another roll says more than the error a job on it left ("replace
  // media", the printer's answer to the label of 06.11.2026).
  assert(brotherQlRollVerdict(seen(62, 0x0B, 29, 0, 0x01), r62c) == BQV_OTHER);
  assert(brotherQlRollVerdict(seen(29, 0x0B, 90, 0, 0x01), r62c) == BQV_UNKNOWN);
  // The other errors, on the roll that is set: cutter jam, printer off,
  // replace media, expansion buffer full, communication, system error.
  for (uint8_t e1 : {0x04, 0x20}) assert(brotherQlRollVerdict(seen(62, 0x0B, 29, e1), r6229) == BQV_ERROR);
  for (uint8_t e2 : {0x01, 0x02, 0x04, 0x80}) assert(brotherQlRollVerdict(seen(62, 0x0B, 29, 0, e2), r6229) == BQV_ERROR);
  // Not errors: "printer in use" (it prints another job; the label waits
  // its turn as before) and the bits Brother marks as not used.
  for (uint8_t e1 : {0x08, 0x10, 0x40, 0x80}) assert(brotherQlRollVerdict(seen(62, 0x0B, 29, e1), r6229) == BQV_OK);
  for (uint8_t e2 : {0x08, 0x20}) assert(brotherQlRollVerdict(seen(62, 0x0B, 29, 0, e2), r6229) == BQV_OK);
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "brother_ql_status.cpp"),
                    str(root / "src" / "services" / "brother_ql_rules.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("brother ql status: ok")
