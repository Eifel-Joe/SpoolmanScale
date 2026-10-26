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
//  brother_ql_rules.h), then a wait for the printer to close
//  the line. Like the Bluetooth drivers it blocks the loop
//  while it runs, with short limits: 3 s to connect, 10 s to
//  wait. It starts no task of its own.
//
//  It never sends twice. The printer says nothing back on this
//  port, so "sent" is all there is to know; a job that met an
//  open lid or an empty roll is held and printed later (befunde
//  3), and a second send would print it twice.
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

// `host` is the printer's IPv4 address as typed. Blocking, from appLoop()
// only. `progress` is called about once per row and once per wait round.
BrotherQlResult brotherQlPrint(const char* host, const BrotherQlLabel& label,
                               const LabelRaster& image, BleProgressFn progress);

// Where a run stands, for the print card; idle outside a run. The same two
// calls the Bluetooth session has, so the card does not care which it asks.
BleSessionPhase brotherQlPhase();
void brotherQlBytes(size_t* sent, size_t* total);
