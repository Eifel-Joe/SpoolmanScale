#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
//  TSPL JOB RULES
//
//  The bytes of one TSPL label job, apart from the Bluetooth
//  that carries them (services/tspl_printer.cpp), so the host
//  test checks them byte for byte (test/test_tspl_job.py).
//  Read off AreYouThereAndi2's capture of the PM240's vendor
//  app (Labelnize, 17.10.2026): SIZE, GAP, DIRECTION, DENSITY,
//  CLS, one BITMAP with the rows, PRINT.
// ============================================================

// The print row of the PM240's head: 48 mm, 384 dots at 203 dpi. The app
// sends 48 bytes a row for a 50 mm label, centred on it.
#define TSPL_HEAD_DOTS 384
// What the app sent for the 50 x 30 mm roll of the capture: 6 mm between
// labels, heat 10 of 15.
#define TSPL_GAP_MM   6
#define TSPL_DENSITY  10

// The part of each raster row the head prints: row_bytes bytes, starting
// skip bytes into the row. All of it when it fits, else its middle.
struct TsplCut { uint16_t row_bytes; uint16_t skip; };
TsplCut tsplCut(uint16_t raster_width, uint16_t raster_row_bytes);

// One job's label: its size in mm and the rows that go to the head.
struct TsplLabel { uint16_t media_w_mm, media_l_mm, row_bytes, rows; };

// The text before the rows, up to and with "BITMAP x,y,w,h,1,". Its length;
// 0 when it does not fit in cap, so a job is never sent cut.
size_t tsplHead(char* out, size_t cap, const TsplLabel& label);

// One row as TSPL's BITMAP reads it: a 0 bit is a black dot, the raster's 1.
void tsplRow(const uint8_t* raster_row, const TsplCut& cut, uint8_t* out);

// After the rows: print one.
extern const char TSPL_TAIL[];
size_t tsplTailLen();
