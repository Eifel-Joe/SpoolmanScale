#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "services/label_printer.h"

// The Brother on the network, where the printer page has its device tile
// while the label printer is set to WLAN: the printer, the field for its IP
// address and the one thing to set on the printer itself. The address is
// typed here only, never on the scale (design L1, ruling 2).

// The tile (id ht, its two lines hn and hd filled by the script) and the
// field under it: the address input hi, its save button hs, the message line
// hs-s and the hint on the printer's command mode, "Raster".
void webPrinterHostTile(String& h, const LabelPrinterConfig& c);

// Defines host(d), which paints the tile from the page's state (d.host) and
// does nothing on a page without it, and binds hs and Enter in hi. Uses the
// page's P and load() and app.js.
void webPrinterHostScript(String& h);

// POST /api/printer/host: an IPv4 address, or nothing for none.
void webPrinterHostRoutes(WebServer& srv);
