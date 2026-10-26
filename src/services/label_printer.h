#pragma once

#include <stddef.h>
#include <stdint.h>

#include "services/ble_service.h"
#include "services/label_raster.h"

// ============================================================
//  THE LABEL PRINTER
//
//  Which printer the scale has, what label stock is in it, and
//  the one call that puts a raster on paper. The printer is a
//  BLE device picked on the device card; the model and the
//  loaded label size are set on the printer screen or in the
//  browser. Models are profiles: head width, the label sizes
//  they take, whether anyone has printed on one yet.
//
//  Or it is a Brother QL on the network, when the label printer
//  is set to WLAN (LabelPrinterLink). Each keeps its settings
//  under keys of its own, and labelPrinterLoadConfig() hands out
//  the one that is chosen: everything that draws, offsets or
//  prints works on that one and need not ask which it is.
//
//  Millimetres are what a person reads off the label roll; dots
//  are what the head wants, at the profile's resolution. The
//  conversions live here so the renderer and the screens agree.
// ============================================================

// The values are NVS content (printer_model): a new model goes at the end,
// none is ever renumbered or reused.
enum LabelPrinterModel : uint8_t {
  LP_MODEL_NONE = 0, LP_MODEL_M220 = 1, LP_MODEL_M110 = 2, LP_MODEL_M100 = 3,
  LP_MODEL_NIIMBOT_B = 4,     // the 50 mm NIIMBOT class: B1, B21, B203 ...
  LP_MODEL_NIIMBOT_M2 = 5,    // NIIMBOT M2, 300 dpi, thermal transfer
  LP_MODEL_M120 = 6, LP_MODEL_M200 = 7, LP_MODEL_M221 = 8,
  LP_MODEL_M110S = 9,         // the M110's bytes, a row as wide as the label
  LP_MODEL_BROTHER_QL820 = 10 // Brother QL-810W/820NWB over WLAN, see services/brother_ql.h
};

// How the scale reaches its label printer: not at all, over Bluetooth, or over
// WLAN to a Brother QL. The values are NVS content (printer_link): a new link
// goes at the end, none is ever renumbered or reused.
enum LabelPrinterLink : uint8_t { LP_LINK_OFF = 0, LP_LINK_BLE = 1, LP_LINK_WLAN = 2 };

// The link a scale is left on after an update, from what printer_link holds
// (-1 for no key yet, which is every scale before this choice existed) and
// the Bluetooth switch (ble_on). WLAN counts only when it was stored; for
// anything else the Bluetooth switch decides, so the choice stands on what
// was true before: Bluetooth on is the Bluetooth printer, off is none.
inline LabelPrinterLink labelPrinterLinkFrom(int stored, bool ble_on) {
  if (stored == LP_LINK_WLAN) return LP_LINK_WLAN;
  return ble_on ? LP_LINK_BLE : LP_LINK_OFF;
}

// Which bytes a model takes. Phomemo's two preambles share one write-only
// transport; a NIIMBOT talks back and gets a driver of its own; a Brother QL
// is reached over WLAN, not over Bluetooth.
enum LabelPrinterProtocol : uint8_t {
  LP_PROTO_PHOMEMO_M220 = 0, LP_PROTO_PHOMEMO_M110 = 1, LP_PROTO_NIIMBOT = 2,
  LP_PROTO_BROTHER_QL = 3
};

struct LabelPrinterProfile {
  LabelPrinterModel model;
  const char* brand;
  const char* name;
  uint16_t default_width_mm, default_length_mm;
  uint16_t min_width_mm, max_width_mm;
  uint16_t min_length_mm, max_length_mm;
  uint16_t base_raster_width, max_raster_width;   // dots in one print row
  bool     experimental;
  // Prints by heat on heat-sensitive paper, no ribbon: the label turns black
  // wherever it gets hot enough, a spool in the dryer included.
  bool     direct_thermal;
  uint16_t dpi;                 // the head's resolution: 203 or 300
  LabelPrinterProtocol protocol;
};

struct LabelPrinterConfig {
  LabelPrinterModel model;
  char     name[BLE_NAME_LEN];
  char     address[BLE_ADDR_LEN];
  uint16_t media_width_mm, media_length_mm;      // across the head, along the feed
  // Where the stock runs under the head, in dots from the middle of the print
  // row, positive to the right as the label reads. Stored as set and clamped
  // to the row when used, so LP_OFFSET_RIGHT keeps a roll against the right
  // wall there for every label width.
  int16_t  x_offset;
};

// "Against the wall": beyond any head, clamped to the row's edge when used.
#define LP_OFFSET_RIGHT  LABEL_RASTER_MAX_W
#define LP_OFFSET_LEFT  (-LABEL_RASTER_MAX_W)

// The label sizes offered on the printer screen: width across the head by
// length along the feed, in mm. What the vendors sell for these printers.
// The Bluetooth printers' list; a Brother's sizes come from its label table,
// see labelPrinterMediaSizes().
struct LabelMediaSize { uint8_t width_mm, length_mm; };
extern const LabelMediaSize LABEL_MEDIA_SIZES[];
extern const int LABEL_MEDIA_SIZE_COUNT;
// Sizes on sale for these printers that no layout of ours fits yet: the
// printer screen shows them greyed, so the step to them is visible.
extern const LabelMediaSize LABEL_MEDIA_SIZES_PLANNED[];
extern const int LABEL_MEDIA_PLANNED_COUNT;

// The models in the order the printer screen's grid and the browser list
// them: proven first, experimental after. NONE is never in it. The Bluetooth
// printers only; labelPrinterModels() gives the list of a link.
extern const LabelPrinterModel LABEL_PRINTER_MODELS[];
extern const int LABEL_PRINTER_MODEL_COUNT;

enum LabelPrintResult : uint8_t {
  LP_OK = 0,
  LP_NO_PRINTER,        // no device picked, or no model
  LP_BAD_RASTER,        // shape or padding wrong
  LP_TOO_WIDE,          // wider than the head
  LP_MEDIA_MISMATCH,    // not the loaded label size
  LP_BLE_OFF,
  LP_BLE_INIT,
  LP_BLE_CONNECT,
  LP_BLE_CHARACTERISTIC,
  LP_BLE_WRITE,
  LP_BLE_STUCK,
  LP_SENT_UNCONFIRMED,  // all sent, the printer never said done
  // What a printer that talks back can say, see services/niimbot.h.
  LP_BLE_NOT_NIIMBOT,   // nothing of the protocol's shape on the device
  LP_PRN_COVER,
  LP_PRN_NO_PAPER,
  LP_PRN_NO_RIBBON,
  LP_PRN_BUSY,
  LP_PRN_ERROR,         // another error code; it is in the log
  LP_MODEL_MISMATCH,    // a known printer, but not of the picked class
  LP_MODEL_UNKNOWN,     // an id the table does not have; it is in the log
  LP_SERVER_LABEL,      // the server did not draw the chosen preset; why is in the log
  // What a Brother on the network can come to, see services/brother_ql.h.
  LP_NET_SENT,          // all bytes handed over; the printer says nothing back
  LP_NET_NO_WIFI,       // the scale is not on a network
  LP_NET_CONNECT,       // nobody answered at the address within 3 s
  LP_NET_ABORT,         // the line broke mid-job; nothing is resent
  // The label printer is set to off. Not LP_LINK_OFF: that is the link's own
  // value above, and two enumerators of one name do not compile.
  LP_PRINTER_OFF,
};

const LabelPrinterProfile& labelPrinterProfile(LabelPrinterModel model);

// The chosen link. printer_link is read once and kept; without the key, which
// is every scale before this choice existed, the Bluetooth switch decides
// (labelPrinterLinkFrom), and nothing is written until a link is chosen.
LabelPrinterLink labelPrinterLink();
// Writes printer_link. Bluetooth switches the stack's master switch on (it
// takes a restart when this boot did not keep its memory, see
// bleStackAvailable()); off and WLAN switch it off. Forgetting the scanned
// device list is the caller's (bleDevicesForget()).
bool labelPrinterSetLink(LabelPrinterLink link);
// The Brother's address: "" for none or an IPv4 address as brotherQlHostValid
// takes it. Anything else is refused and nothing is written.
bool labelPrinterSetHost(const char* host);

// The chosen printer: the Brother under WLAN, the Bluetooth printer otherwise.
// Each is cached after the first read; every write goes through the save,
// which writes the chosen printer's keys only. Under WLAN model and label are
// the Brother's whatever the caller sends, and an address that is not an IPv4
// one fails the save; on the Bluetooth side a Brother model counts as none.
LabelPrinterConfig labelPrinterLoadConfig();
bool labelPrinterSaveConfig(const LabelPrinterConfig& config);
bool labelPrinterConfigured(const LabelPrinterConfig& config);
// The models we have on the desk and test every label feature on first; the
// manual's page of recommended printers names the same ones.
bool labelPrinterRecommended(LabelPrinterModel model);
// Drops the device, keeps model and label size; under WLAN the Brother's
// address.
bool labelPrinterForget();
// True when this address is the picked printer.
bool labelPrinterIsDevice(const char* address);

// Millimetres to dots at the model's resolution; 0 for no model.
uint16_t labelPrinterDotsForMm(LabelPrinterModel model, uint16_t mm);
// The print row for this model and label width: the head's base width or
// the label, whichever is wider, rounded up to whole bytes, capped at the head.
uint16_t labelPrinterRasterWidth(LabelPrinterModel model, uint16_t media_width_mm);
// The label's dots across the head, never more than the print row: a roll
// wider than the head prints the head's width of it. A Brother's are what its
// label table says it can print across the roll.
uint16_t labelPrinterContentWidth(LabelPrinterModel model, uint16_t media_width_mm);
// The label's dots along the feed: its length converted, or for a Brother the
// lines its label table gives; 0 for a size that table lacks.
uint16_t labelPrinterContentHeight(LabelPrinterModel model, uint16_t width_mm,
                                   uint16_t length_mm);
// The first dot of the label in the print row: the middle plus the offset,
// clamped so the label stays in the row.
uint16_t labelPrinterContentX(const LabelPrinterConfig& config);
// The offsets the loaded stock allows: the label at the row's left edge and
// at its right edge. Both 0 when the label fills the row.
void labelPrinterOffsetRange(const LabelPrinterConfig& config, int16_t* min, int16_t* max);
// The offset in effect: the stored one, clamped to that range.
int16_t labelPrinterOffset(const LabelPrinterConfig& config);
bool labelPrinterRasterFits(LabelPrinterModel model, const LabelRaster& image,
                            uint16_t media_width_mm, uint16_t media_length_mm);

// Blocking, from appLoop() only: checks the raster against the printer and
// the loaded label, then prints. progress keeps an overlay moving.
LabelPrintResult labelPrinterPrint(const LabelPrinterConfig& config,
                                   const LabelRaster& image, BleProgressFn progress);
// The StringID that says what a result means, for a popup or a status line.
int labelPrintResultString(LabelPrintResult result);

// Whether a print can start now on the chosen printer: LP_OK, or what stands
// in the way - off, no device or no address, a Bluetooth stack that needs a
// restart (LP_BLE_INIT) or hangs (LP_BLE_STUCK). The one question behind
// every print button, on the scale and in the browser.
LabelPrintResult labelPrinterReadiness();
// Where a print on the chosen printer stands, for the card while it blocks
// the loop: the Brother's driver under WLAN, the Bluetooth session otherwise.
BleSessionPhase labelPrinterSessionPhase();
void labelPrinterSessionBytes(size_t* sent, size_t* total);

// The models a link offers: the Brother under WLAN, else LABEL_PRINTER_MODELS.
const LabelPrinterModel* labelPrinterModels(LabelPrinterLink link, int* count);
// The label sizes offered for a model: a Brother's from its label table, else
// LABEL_MEDIA_SIZES.
const LabelMediaSize* labelPrinterMediaSizes(LabelPrinterModel model, int* count);

// Whether the model takes label stock of this size.
bool labelPrinterTakesMedia(LabelPrinterModel model, uint16_t width_mm, uint16_t length_mm);

// The next stock size the model can take, after the current one; the first
// fitting one when the current one is not in the model's list
// (labelPrinterMediaSizes). Only the config.
void labelPrinterNextMedia(LabelPrinterConfig& c);

// Whether "Print label" on the scale shows the print sheet first (preview,
// kind, arrangement) or prints at once. On until switched off.
bool labelPrinterAskFirst();
bool labelPrinterSetAskFirst(bool ask);
