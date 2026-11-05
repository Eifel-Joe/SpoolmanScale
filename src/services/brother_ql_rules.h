#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
//  A BROTHER QL JOB: THE RULES
//
//  What a Brother QL-810W/820NWB takes on its raw socket for one
//  label, in black only: the bytes before the first row, the
//  mirrored row, the printable area of a DK label and the check
//  of the printer's address. Since v0.8.7-beta.7 (Spec V75) also
//  the QL-700, which has only USB and is reached through a small
//  computer that forwards port 9100 to it, and four rolls. The driver (services/brother_ql.*)
//  opens the socket and sends these bytes; the rules live here,
//  without Arduino and without the network, so that a host test
//  (test_brother_ql.py) can hold them to the printer's manual.
//
//  Sources:
//    R        Brother "Raster Command Reference QL-800/810W/820NWB"
//             v1.01, p.5 (the example job), p.28-35 (the commands
//             and what each byte means).
//    befunde  docs/recherche/2026-10-26-brother/befunde.md, sections
//             1 (the job, byte by byte, 25 642 bytes for 62 x 29)
//             and 2 (the rows: 720 pins, dot 0 on the right).
//    design   docs/entwurf/2026-10-26-l1-brother-wlan.md, rulings 4
//             (printable area per label, a small table that takes
//             more labels), 5 (the driver and its bytes) and 11
//             (the rules with a host test).
//
//  After the job the driver keeps the line open for a while and
//  the loop looks at it (brotherQlHoldVerdict below): what one
//  look found decides whether to look again, and what the
//  printer sent goes on the card as brotherQlBytesHex() writes it.
//
//  A job is, in this order:
//    brotherQlInvalidate() null bytes, brotherQlStart()'s 38 bytes
//    (32 on the QL-700), one brotherQlRow() (93 bytes) for every line
//    of the label, and BROTHER_QL_PRINT. No compression and no
//    blank-line shortcut (5A): that is a TIFF compression command,
//    which the QL-800 does not know (R p.7, p.31).
//
//  More sources for the rolls and the QL-700 (v0.8.7-beta.7):
//    Q        Brother "QL Series Command Reference (QL-500/550/560/
//             570/580N/650TD/700/1050/1060N)" v6.0: p.4, p.6-10
//             (print areas, pins), p.18, p.21-25 (commands, flow).
//    bql      pklaus/brother_ql 0dee94b: labels.py lines 86, 89, 95,
//             102; models.py line 52 (QL-700: no mode setting, no
//             compression); conversion.py, raster.py 110-262.
//    f08      Ziopase/SpoolmanScale d9e3a89d, c96098d7: the rolls,
//             the print position a few mm either way.
// ============================================================

// The raw socket of the printer (befunde 3 and 5: port 9100).
#define BROTHER_QL_PORT         9100
// Null bytes before the job: they flush what an earlier, broken job left in
// the printer's buffer (R p.5). The QL-700's flow chart sends 200 (Q p.25).
#define BROTHER_QL_INVALIDATE   400
#define BROTHER_QL700_INVALIDATE 200
// The head has 720 pins, 8 to a byte (R p.19).
#define BROTHER_QL_HEAD_DOTS    720
#define BROTHER_QL_ROW_BYTES    90
// A line on the wire: 67 00 5A, then the 90 bytes of the row (R p.30).
#define BROTHER_QL_LINE_BYTES   93
// Initialize, raster mode, print information, auto cut, cut each, expanded
// mode, margin and compression mode (befunde 1, steps 2 to 9). The QL-700
// takes neither the raster-mode switch nor the compression command (Q p.4,
// p.18, p.21: "only used with QL-580N/650TD/1050/1060N"; bql leaves both
// out for it): 38 - 4 - 2.
#define BROTHER_QL_START_BYTES  38
#define BROTHER_QL700_START_BYTES 32
// The last page: print and feed (R p.31; 0C would be "more pages follow").
#define BROTHER_QL_PRINT        0x1A

// The media type byte of the print information (R p.32).
#define BROTHER_QL_DIE_CUT      0x0B
#define BROTHER_QL_CONTINUOUS   0x0A
// The feed margin of continuous tape, 3 mm: the printer feeds it before and
// after the print (R p.16, p.18, p.28; bql feed_margin=35). A die-cut label
// takes 0, its own gap is the margin.
#define BROTHER_QL_FEED_DOTS    35
// How far the print position may move either way from where the roll's
// table puts it: 5 mm at 300 dpi (f08: DK-11221 came out 3 to 4 mm off on a
// QL-700), and never past the row's ends.
#define BROTHER_QL_OFFSET_DOTS  59

// Which bytes a Brother takes before its rows. The values are not stored.
enum BrotherQlModel : uint8_t {
  BQM_QL800 = 0,   // QL-810W/820NWB (and the QL-800): R
  BQM_QL700 = 1,   // QL-700, behind a forwarder: Q, bql
};

// One DK roll: the size the scale offers it as, the dots the head prints on
// it at 300 dpi across and along the feed, the media type byte, and the pins
// right of the print area in the row as the scale draws it (dot 0 on the
// left; the row goes out mirrored, so these are the first pins the printer
// reads). A continuous roll is offered at one length: its lines are that
// length less the two feed margins.
struct BrotherQlLabel {
  uint8_t  width_mm, length_mm;
  uint16_t dots_w, dots_h;
  uint8_t  media_type;
  uint8_t  margin_r;
};

// The rolls the scale knows. DK-11209 (62 x 29 die-cut) is the first and the
// default; the next ones are added to the table, nothing else changes
// (design ruling 4).
extern const BrotherQlLabel BROTHER_QL_LABELS[];
extern const int BROTHER_QL_LABEL_COUNT;

// The table's entry for a stock size; nullptr for a size the table lacks.
const BrotherQlLabel* brotherQlLabel(uint8_t width_mm, uint8_t length_mm);

// The dots that can be printed across a roll of this width, 0 for a width
// the table lacks.
uint16_t brotherQlPrintWidth(uint8_t width_mm);

// The null bytes in front of a job and the bytes that follow them, by model.
size_t brotherQlInvalidate(BrotherQlModel model);
size_t brotherQlStartBytes(BrotherQlModel model);

// The bytes that follow the null bytes (brotherQlStartBytes()), for a label
// printed in `lines` lines on `model`. 0 and nothing written when `cap` does
// not hold them. Continuous tape: media 0A, length 0 (the lines set it), the
// feed margin; die-cut: media 0B, the label's length, margin 0.
size_t brotherQlStart(const BrotherQlLabel& label, BrotherQlModel model, uint16_t lines,
                      uint8_t* out, size_t cap);

// The first dot of the roll's print area in the 720 dot row as the scale
// draws it: 720 - dots across - pins on the right (R p.19-20: 12/696/12,
// 295/413/12, 442/236/42).
uint16_t brotherQlContentX(const BrotherQlLabel& label);
// How far the print position may move from there: BROTHER_QL_OFFSET_DOTS
// either way, as far as the row goes. Negative is to the left.
void brotherQlOffsetRange(const BrotherQlLabel& label, int16_t* min, int16_t* max);
// The NVS key a roll's print position is kept under: "px_brother" for
// DK-11209, as it always was, "px_bq_<w><l>" for the others. False when
// `n` does not hold it.
bool brotherQlOffsetKey(const BrotherQlLabel& label, char* out, size_t n);

// One line of the job, BROTHER_QL_LINE_BYTES into `out`, from one raster row
// of BROTHER_QL_ROW_BYTES (dot 0 left, most significant bit first, as in
// label_raster.h). The printer counts its pins from the right, so the row is
// mirrored: the bytes in reverse order and the bits of every byte reversed.
void brotherQlRow(const uint8_t* row, uint8_t* out);

// What a whole job comes to for `lines` lines on `model`, null bytes and
// print byte included (25 642 for the 271 lines of a 62 x 29 label on a
// QL-810W/820NWB).
size_t brotherQlJobBytes(BrotherQlModel model, uint16_t lines);

// Whether this is an IPv4 address as a person types it: four numbers of 0 to
// 255 with dots, 1 to 3 digits each and no leading zero, the first one not 0.
// Nothing else passes, so no blank and no name (the socket is opened by
// address, not by lookup).
bool brotherQlHostValid(const char* host);

// How long the line stays open after the last byte, at most (Spec 5.3 L1).
// A QL-810W closes it once it has read the job, and closing first leaves it
// busy (befunde 3); the QL-820NWBc keeps it open. Through this time the loop
// looks at the line once per pass instead of waiting for it.
#define BROTHER_QL_HOLD_MS      10000

// What one look at the held line found. The look is a recv() that does not
// wait: `got` is its result (bytes read, 0 for the printer's close, -1 for
// none), `would_block` says that a -1 only means nothing has come yet, and
// `held_ms` is the time since the last byte went out.
enum BrotherQlHold : uint8_t {
  BQH_WAIT = 0,  // nothing yet, look again on the next pass
  BQH_CLOSED,    // the printer closed the line without a word: its normal end
  BQH_DATA,      // the printer sent bytes, whenever they come: they are shown
  BQH_ABORT,     // the line broke (a reset or another error): that is shown
  BQH_TIMEOUT,   // BROTHER_QL_HOLD_MS in silence: the scale closes the line
};
BrotherQlHold brotherQlHoldVerdict(int got, bool would_block, uint32_t held_ms);

// The bytes as two upper-case hex digits each, a blank between them, into
// `out`, always terminated: as many whole bytes as `out_len` holds. Returns
// the characters written, 0 (and "" where there is room) for no bytes or no
// buffer.
size_t brotherQlBytesHex(const uint8_t* bytes, size_t n, char* out, size_t out_len);
