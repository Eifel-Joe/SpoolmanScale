#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
//  A BROTHER QL JOB: THE RULES
//
//  What a Brother QL-810W/820NWB takes on its raw socket for one
//  label, in black only: the bytes before the first row, the
//  mirrored row, the printable area of a DK label and the check
//  of the printer's address. The driver (services/brother_ql.*)
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
//  A job is, in this order:
//    BROTHER_QL_INVALIDATE null bytes, brotherQlStart()'s 38 bytes,
//    one brotherQlRow() (93 bytes) for every line of the label, and
//    BROTHER_QL_PRINT. No compression and no blank-line shortcut
//    (5A): that is a TIFF compression command, which the QL-800
//    does not know (R p.7, p.31).
// ============================================================

// The raw socket of the printer (befunde 3 and 5: port 9100).
#define BROTHER_QL_PORT         9100
// Null bytes before the job: they flush what an earlier, broken job left in
// the printer's buffer (R p.5).
#define BROTHER_QL_INVALIDATE   400
// The head has 720 pins, 8 to a byte (R p.19).
#define BROTHER_QL_HEAD_DOTS    720
#define BROTHER_QL_ROW_BYTES    90
// A line on the wire: 67 00 5A, then the 90 bytes of the row (R p.30).
#define BROTHER_QL_LINE_BYTES   93
// Initialize, raster mode, print information, auto cut, cut each, expanded
// mode, margin and compression mode (befunde 1, steps 2 to 9).
#define BROTHER_QL_START_BYTES  38
// The last page: print and feed (R p.31; 0C would be "more pages follow").
#define BROTHER_QL_PRINT        0x1A

// One DK label: the stock's size as printed on the roll, the dots the head
// can print on it at 300 dpi (without the margins the roll has), and the
// media type byte of the print information (0x0B is die-cut, befunde 1).
struct BrotherQlLabel {
  uint8_t  width_mm, length_mm;
  uint16_t dots_w, dots_h;
  uint8_t  media_type;
};

// The labels the scale knows. DK-11209 (62 x 29 die-cut) is the first; the
// next ones are added to the table, nothing else changes (design ruling 4).
extern const BrotherQlLabel BROTHER_QL_LABELS[];
extern const int BROTHER_QL_LABEL_COUNT;

// The table's entry for a stock size; nullptr for a size the table lacks.
const BrotherQlLabel* brotherQlLabel(uint8_t width_mm, uint8_t length_mm);

// The dots that can be printed across a roll of this width, 0 for a width
// the table lacks.
uint16_t brotherQlPrintWidth(uint8_t width_mm);

// The 38 bytes that follow the null bytes, for a label printed in `lines`
// lines. 0 and nothing written when `cap` does not hold them.
size_t brotherQlStart(const BrotherQlLabel& label, uint16_t lines, uint8_t* out, size_t cap);

// One line of the job, BROTHER_QL_LINE_BYTES into `out`, from one raster row
// of BROTHER_QL_ROW_BYTES (dot 0 left, most significant bit first, as in
// label_raster.h). The printer counts its pins from the right, so the row is
// mirrored: the bytes in reverse order and the bits of every byte reversed.
void brotherQlRow(const uint8_t* row, uint8_t* out);

// What a whole job comes to for `lines` lines, null bytes and print byte
// included (25 642 for the 271 lines of a 62 x 29 label).
size_t brotherQlJobBytes(uint16_t lines);

// Whether this is an IPv4 address as a person types it: four numbers of 0 to
// 255 with dots, 1 to 3 digits each and no leading zero, the first one not 0.
// Nothing else passes, so no blank and no name (the socket is opened by
// address, not by lookup).
bool brotherQlHostValid(const char* host);
