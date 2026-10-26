#pragma once

#include <Arduino.h>

#include "services/label_printer.h"

// The printer page's own look and its two card pickers. The page builds the
// layout and the script; the pieces that are the same for every visit live
// here, so the page file stays about what it does.

// One <style> block: the label printer's bar, the device tile, the scan
// list and the card pickers, all on the shared colour variables.
void webPrinterPageStyle(String& h);

// The printer as an inline SVG, for the bar and the printer's tile.
void webPrinterIcon(String& h);

// A card per model of the chosen link (labelPrinterModels) in the table's
// order, brand, name, head width and resolution, recommended and
// experimental ones tagged. data-m is the model the route takes; the current
// one starts marked.
void webPrinterModelCards(String& h, const LabelPrinterConfig& c);

// A card per label size of the model (labelPrinterMediaSizes), its outline
// drawn to scale, the Bluetooth printers' planned ones greyed. data-fit lists
// the models that take a size, so the script shows the ones the picked model
// can print on without asking the scale again.
void webPrinterStockCards(String& h, const LabelPrinterConfig& c);
