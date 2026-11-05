#pragma once

#include <stdint.h>

#include "services/ble_service.h"
#include "services/label_printer.h"

// ============================================================
//  WAS THE BLUETOOTH PRINTER THERE?
//
//  The Bluetooth symbol in the header turns green when the printer
//  set up was found by the last scan or took the last print, grey
//  when it could not be reached then (Spec 5.3 AC f21, V74; the
//  idea from akira69's fork, built anew). The rules apart from the
//  state, for test/test_printer_reach.py; the state is in
//  label_printer.cpp, in memory only.
// ============================================================

enum LabelPrinterReach : uint8_t { LP_REACH_KEEP = 0, LP_REACH_YES = 1, LP_REACH_NO = 2 };

// What a print's result says. The printer answered, its own complaint
// ("no paper", "cover open", another class) included: it is there. Not
// reached, or the device is no printer of ours: grey. A print that never
// went on the air (refused before, or the Brother's over WLAN) says nothing.
inline LabelPrinterReach labelPrintReach(LabelPrintResult r) {
  switch (r) {
    case LP_OK: case LP_SENT_UNCONFIRMED:
    case LP_PRN_COVER: case LP_PRN_NO_PAPER: case LP_PRN_NO_RIBBON: case LP_PRN_BUSY:
    case LP_PRN_ERROR: case LP_MODEL_MISMATCH: case LP_MODEL_UNKNOWN:
      return LP_REACH_YES;
    case LP_BLE_CONNECT: case LP_BLE_OFF: case LP_BLE_INIT: case LP_BLE_STUCK:
    case LP_BLE_WRITE: case LP_BLE_CHARACTERISTIC: case LP_BLE_NOT_NIIMBOT:
      return LP_REACH_NO;
    default:
      return LP_REACH_KEEP;
  }
}

// Two addresses as the same device, whichever case each was written in.
inline bool labelPrinterSameAddress(const char* a, const char* b) {
  for (;; ++a, ++b) {
    char x = *a, y = *b;
    if (x >= 'A' && x <= 'Z') x = char(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = char(y - 'A' + 'a');
    if (x != y) return false;
    if (!x) return true;
  }
}

// What a scan says: the printer set up among the devices found, or not. No
// printer set up is nothing to be green about. A scan that failed (the
// stack did not start, count < 0) says nothing.
inline LabelPrinterReach labelScanReach(const char* configured, const BleDevice* found, int count) {
  if (count < 0) return LP_REACH_KEEP;
  if (!configured || !configured[0]) return LP_REACH_NO;
  for (int i = 0; i < count; i++)
    if (found && labelPrinterSameAddress(configured, found[i].address)) return LP_REACH_YES;
  return LP_REACH_NO;
}
