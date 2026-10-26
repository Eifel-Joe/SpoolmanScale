#pragma once

#include <stddef.h>
#include <stdint.h>

#include "services/ble_service.h"
#include "services/brother_ql_rules.h"
#include "services/label_raster.h"

// ============================================================
//  A BROTHER QL ON THE NETWORK
//
//  The driver that carries one label to a Brother QL-810W or
//  820NWB: a TCP connection to its raw port, the job's bytes
//  produced row by row from the raster (the rules are in
//  brother_ql_rules.h), then the line held open for the
//  printer. Like the Bluetooth drivers it blocks the loop while
//  it connects and sends, 3 s at most to connect. It starts no
//  task of its own.
//
//  The hold. Wurzel: a QL-810W closes the line once it has read
//  the job, and a sender that closes first leaves it busy
//  (befunde 3), so the driver used to wait in a loop for the
//  close, up to 10 s; Joerg's QL-820NWBc never closes it, and
//  every label held the scale for 10.6 s (beta.6, STATUS 49 b).
//  Fix: the line stays open up to BROTHER_QL_HOLD_MS, but the
//  loop looks at it once per pass (brotherQlTick()), and the
//  scale is free as soon as the bytes are out. The printer's
//  close ends the hold quietly; bytes from it, or a line that
//  breaks, are handed to the screen (brotherQlTakeReply()). The
//  next print attempt closes a held line first, and a new label
//  goes out at once (Joerg, 26.10.2026). NOT-TO-DO: a task of its own for the wait (Spec
//  5.3 L1, V27: none); closing right after the last byte, which
//  is what leaves the QL-810W busy.
//
//  It never sends twice. The printer says nothing back on this
//  port as a rule, so "sent" is all there is to know; a job that
//  met an open lid or an empty roll is held and printed later
//  (befunde 3), and a second send would print it twice.
//
//  Free of Arduino on purpose, like ble_service.h: the screens
//  include this, and the simulator builds them against a fake.
// ============================================================

enum BrotherQlResult : uint8_t {
  BQ_SENT = 0,   // all bytes handed over; the printer has them or holds them
  BQ_BAD_RASTER, // not the 90 bytes by label.dots_h rows a Brother takes
  BQ_NO_WIFI,    // the scale is not on a network
  BQ_CONNECT,    // no valid address, or nobody answered within 3 s
  BQ_WRITE,      // the line broke or stalled mid-job; nothing is resent
};

// `host` is the printer's IPv4 address as typed. Blocking while it connects
// and sends, from appLoop() only; a line still held from the job before is
// closed first (brotherQlRelease()). `progress` is called about once per row.
// BQ_SENT leaves the line held (brotherQlTick()).
BrotherQlResult brotherQlPrint(const char* host, const BrotherQlLabel& label,
                               const LabelRaster& image, BleProgressFn progress);

// Looks at the held line, at most every 50 ms, and ends the hold when the
// printer closes it, the line breaks or BROTHER_QL_HOLD_MS are over. From the
// loop on every pass; nothing to do without a hold. The end is in the log.
void brotherQlTick();
// Ends a held line before another print attempt, whichever printer it goes
// to and however it ends, and drops what the printer said meanwhile that the
// screen has not taken: a late reply belongs to the label before, and must
// not take the place of this attempt's answer. From the loop, at the start
// of every print attempt; brotherQlPrint() calls it as well.
void brotherQlRelease();

// What the printer said while its line was held.
enum BrotherQlReplyKind : uint8_t {
  BQR_NONE = 0,
  BQR_BYTES,    // it sent bytes; the first chunk is in `bytes`
  BQR_BROKEN,   // the line broke before any bytes came (a reset, a lost
                // network or another error, in `err`)
};
#define BROTHER_QL_REPLY_MAX 32
struct BrotherQlReply {
  BrotherQlReplyKind kind;
  uint8_t            n;                            // bytes in `bytes`
  uint8_t            bytes[BROTHER_QL_REPLY_MAX];
  int                err;                          // errno of a broken line, else 0
};
// Hands out what the printer said during the hold, once; false when it has
// said nothing new. Asked right after brotherQlTick().
bool brotherQlTakeReply(BrotherQlReply* out);

// Where a run stands, for the print card; idle outside a run. The same two
// calls the Bluetooth session has, so the card does not care which it asks.
BleSessionPhase brotherQlPhase();
void brotherQlBytes(size_t* sent, size_t* total);
