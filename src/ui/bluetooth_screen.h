#pragma once

#include "services/label_printer.h"

// ============================================================
//  LABEL PRINTER
//
//  The screen behind the label printer tile on the Connection
//  screen: off, Bluetooth or WLAN, and under the choice that
//  printer's rows - the device list (ui/ble_devices_screen.cpp)
//  and the printer screen for Bluetooth, the Brother and a test
//  print for WLAN. It was the Bluetooth screen, and keeps that
//  screen's file and names.
// ============================================================

void buildBluetoothScreen();
void closeBluetoothScreen();
// The rebuild after a choice, from appLoop(): it deletes the screen the row
// sits on, so not from the row's own callback.
void handleBluetoothDeferredActions();

// The sign and the word of a choice: this screen's rows and the tile.
const char* labelPrinterLinkSymbol(LabelPrinterLink link);
int labelPrinterLinkTitle(LabelPrinterLink link);
