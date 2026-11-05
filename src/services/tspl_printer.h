#pragma once

#include <stdint.h>

#include "services/ble_service.h"
#include "services/label_raster.h"

// ============================================================
//  TSPL LABEL PRINTERS OVER BLE
//
//  The TYPONOS PM240 takes TSPL, the text command language of
//  TSC-style label printers: a few lines that set label size,
//  gap and heat, one BITMAP with the rows, PRINT. Read off a
//  Bluetooth capture of the vendor app (Labelnize, 17.10.2026),
//  which sends it over classic SPP; the printer also offers the
//  same GATT service the Phomemo printers use, and that is the
//  way in for the ESP32-S3, which has no classic Bluetooth.
//  The bytes of the job: services/tspl_rules.h.
//
//  From AreYouThereAndi2's fork; never printed on a real PM240
//  yet, only checked against the app's capture: experimental.
// ============================================================

// The service and characteristic the PM240 lists, the Phomemo pair; ff02 is
// write without response (nRF Connect, 17.10.2026).
#define TSPL_SERVICE_UUID 0xff00
#define TSPL_WRITE_UUID   0xff02

// Sends one raster as a TSPL job for a label of media_w x media_l mm. A row
// wider than the head is cut to its middle 384 dots, the part the head sees.
BleWriteResult tsplPrint(const char* address, const LabelRaster& image,
                         uint16_t media_w_mm, uint16_t media_l_mm, BleProgressFn progress);
