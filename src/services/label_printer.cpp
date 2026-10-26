#include "services/label_printer.h"

#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/brother_ql.h"
#include "services/niimbot.h"
#include "services/phomemo_m_series.h"
#include "services/prefs_store.h"

// The NVS keys. 15 characters at most, and never renamed: they sit in the
// NVS of every device that ever picked a printer.
#define LP_KEY_MODEL "printer_model"
#define LP_KEY_ADDR  "printer_addr"
#define LP_KEY_NAME  "printer_name"
#define LP_KEY_W     "printer_w"
#define LP_KEY_H     "printer_h"
#define LP_KEY_ASK   "printer_ask"
// The offset belongs to one printer, not to the scale: "px" and the device's
// address without colons, 14 characters. A second printer starts at 0, and
// the first gets its own back when it is picked again (Nikolai, 30.09.2026).
#define LP_KEY_XOFF_PREFIX "px"
// The link (LabelPrinterLink) and the Brother's own settings, apart from the
// Bluetooth printer's, so each survives the other being chosen. The Brother's
// offset has a fixed key: "px" and an IP address would run past 15
// characters (befunde 7), and "_brother" is no hex, so no device's key is it.
#define LP_KEY_LINK     "printer_link"
#define LP_KEY_HOST     "printer_host"
#define LP_KEY_NET_XOFF "px_brother"
static_assert(sizeof(LP_KEY_LINK) - 1 <= 15, "an NVS key has 15 characters at most");
static_assert(sizeof(LP_KEY_HOST) - 1 <= 15, "an NVS key has 15 characters at most");
static_assert(sizeof(LP_KEY_NET_XOFF) - 1 <= 15, "an NVS key has 15 characters at most");

static const LabelPrinterProfile PROFILE_NONE = {};
// M220: 2 inch head, 576 dots, takes stock from 20 to 75 mm wide; the vendor
// app pads narrower rows to 576 and wider ones to 648. Hardware proven.
static const LabelPrinterProfile PROFILE_M220 = {
  LP_MODEL_M220, "Phomemo", "M220", 40, 30, 20, 75, 10, 150, 576, 648, false, true,
  203, LP_PROTO_PHOMEMO_M220
};
// M110: 48 mm head, 384 dots. Same transport, own preamble. A user printed
// with this profile on an M100 (09.2026), which proves the M110 bytes.
static const LabelPrinterProfile PROFILE_M110 = {
  LP_MODEL_M110, "Phomemo", "M110", 40, 30, 20, 48, 10, 150, 384, 384, false, true,
  203, LP_PROTO_PHOMEMO_M110
};
// M110S: the M110's bytes, but its roll runs against the right wall, and a
// 384 dot row lands about 1 mm left of it even with the label at the row's
// right end (issue #55). Phomemo's own app, and a USB tool written for this
// printer, send a row exactly as wide as the label and let the printer place
// it: a base width of 0 does that here, and leaves no offset to set. The
// reporter's calibration page sat centred on 40 x 30 with beta.63.
static const LabelPrinterProfile PROFILE_M110S = {
  LP_MODEL_M110S, "Phomemo", "M110S", 40, 30, 20, 48, 10, 150, 0, 384, false, true,
  203, LP_PROTO_PHOMEMO_M110
};
// M100: the M110's head and bytes under its own name, so a user finds it.
static const LabelPrinterProfile PROFILE_M100 = {
  LP_MODEL_M100, "Phomemo", "M100", 40, 30, 20, 48, 10, 150, 384, 384, false, true,
  203, LP_PROTO_PHOMEMO_M110
};
// The siblings Phomemo sells the same label rolls for. phomemo-tools shows
// the M120 speaking the M110 protocol; the M200 and M221 take the M220's
// 20 to 80 mm stock at 203 dpi. Experimental until a user has printed.
static const LabelPrinterProfile PROFILE_M120 = {
  LP_MODEL_M120, "Phomemo", "M120", 40, 30, 20, 48, 10, 150, 384, 384, true, true,
  203, LP_PROTO_PHOMEMO_M110
};
static const LabelPrinterProfile PROFILE_M200 = {
  LP_MODEL_M200, "Phomemo", "M200", 40, 30, 20, 75, 10, 150, 576, 648, true, true,
  203, LP_PROTO_PHOMEMO_M220
};
static const LabelPrinterProfile PROFILE_M221 = {
  LP_MODEL_M221, "Phomemo", "M221", 40, 30, 20, 75, 10, 150, 576, 648, true, true,
  203, LP_PROTO_PHOMEMO_M220
};

// NIIMBOT's 50 mm class: B1, B21, B21S and B21 C2B at 384 dots, the B203 at
// 400, all 203 dpi. The printer says which it is and the driver pads the
// row. Its standard roll is 50 x 30, of which the 48 mm head prints 48. The
// protocol is from niimbluelib. A user printed on a B1 several times, 40 x 30
// (14.10.2026); the B21 and B203 share the driver but have not been tried.
static const LabelPrinterProfile PROFILE_NIIMBOT_B = {
  LP_MODEL_NIIMBOT_B, "NIIMBOT", "B1, B21, B203", 50, 30, 20, 50, 10, 150, 384, 384, false, true,
  203, LP_PROTO_NIIMBOT
};
// NIIMBOT M2: 300 dpi, 567 dots, which is 71 bytes and so a 568 dot row.
// Prints through a ribbon, so the label survives the dryer. Same protocol,
// the B1's sequence. 50 x 30 is the first roll for it (Nikolai, 21.10.2026),
// of which the 48 mm head prints 48, as on the B1. Only 50 mm wide: NIIMBOT
// sells no 40 x 30 roll for it, and no layout of ours fits a narrower one.
// Hardware proven: Nikolai printed on it with the original white 50 x 30
// roll and black ribbon (22.10.2026).
static const LabelPrinterProfile PROFILE_NIIMBOT_M2 = {
  LP_MODEL_NIIMBOT_M2, "NIIMBOT", "M2", 50, 30, 50, 50, 10, 150, 568, 568, false, false,
  300, LP_PROTO_NIIMBOT
};

// Brother QL-810W and QL-820NWB over WLAN (services/brother_ql.h): 300 dpi,
// a head of 720 pins, 90 bytes to a row. The bytes are from Brother's "Raster
// Command Reference QL-800/810W/820NWB" (R; befunde 1-2). The QL-710W/720NW
// start a job with other bytes and the QL-1110NWB has 1296 pins (befunde 6):
// they get profiles of their own once someone can try one. DK paper darkens
// by heat, no ribbon. The mm spans say DK-11209, 62 x 29, but for this
// protocol the label table in brother_ql_rules.cpp decides what it takes.
// Hardware proven: Joerg printed the test label and a spool label on his
// QL-820NWBc with DK-11209 from v0.8.5-beta.6 (26.10.2026).
static const LabelPrinterProfile PROFILE_BROTHER_QL820 = {
  LP_MODEL_BROTHER_QL820, "Brother", "QL-810W/820NWB", 62, 29, 62, 62, 29, 29, 720, 720,
  false, true /* DK paper is thermal */,
  300, LP_PROTO_BROTHER_QL
};

const LabelPrinterModel LABEL_PRINTER_MODELS[] = {
  LP_MODEL_M220, LP_MODEL_NIIMBOT_M2, LP_MODEL_M110, LP_MODEL_M110S, LP_MODEL_M100,
  LP_MODEL_NIIMBOT_B, LP_MODEL_M120, LP_MODEL_M200, LP_MODEL_M221,
};
const int LABEL_PRINTER_MODEL_COUNT = sizeof(LABEL_PRINTER_MODELS) / sizeof(LABEL_PRINTER_MODELS[0]);
// What the WLAN link offers: the one Brother profile there is.
static const LabelPrinterModel LABEL_PRINTER_MODELS_WLAN[] = { LP_MODEL_BROTHER_QL820 };
static const int LABEL_PRINTER_MODEL_WLAN_COUNT =
  sizeof(LABEL_PRINTER_MODELS_WLAN) / sizeof(LABEL_PRINTER_MODELS_WLAN[0]);

// The two sizes printed and checked on the M220 for 0.8.0 (Nikolai,
// 26.09.2026). 40 x 20 and 30 x 20 leave no room for the code; the larger
// ones come back with the label presets.
const LabelMediaSize LABEL_MEDIA_SIZES[] = {
  {40, 30}, {50, 30},
};
const int LABEL_MEDIA_SIZE_COUNT = sizeof(LABEL_MEDIA_SIZES) / sizeof(LABEL_MEDIA_SIZES[0]);

// 50 x 50 rolls are sold for the M220, the B1 and the M2, but no layout of
// ours is drawn for them yet (Nikolai, 22.10.2026).
const LabelMediaSize LABEL_MEDIA_SIZES_PLANNED[] = {
  {50, 50},
};
const int LABEL_MEDIA_PLANNED_COUNT =
  sizeof(LABEL_MEDIA_SIZES_PLANNED) / sizeof(LABEL_MEDIA_SIZES_PLANNED[0]);

// The Bluetooth printer's config.
static LabelPrinterConfig s_config{};
static bool s_loaded = false;
// The Brother's, cached the same way and written under its own keys.
static LabelPrinterConfig s_net{};
static bool s_net_loaded = false;
// printer_link as stored, -1 for no key; read once.
static int  s_link_stored = -1;
static bool s_link_loaded = false;
// Whether "Print label" opens the print sheet first. Cached like the config.
static bool s_ask_first = true;
static bool s_ask_loaded = false;

// The NVS key of a printer's offset; false for no printer. An address is
// 17 characters, six pairs of hex digits and five colons.
static bool offsetKey(const char* address, char* key, size_t n) {
  if (!address || !address[0]) return false;
  size_t k = snprintf(key, n, "%s", LP_KEY_XOFF_PREFIX);
  for (const char* a = address; *a && k + 1 < n; a++)
    if (*a != ':') key[k++] = (char)tolower((unsigned char)*a);
  key[k] = '\0';
  return true;
}

static int16_t loadOffset(const char* address) {
  char key[20];
  return offsetKey(address, key, sizeof(key)) ? (int16_t)prefsGetInt(key, 0) : 0;
}
// The label size the user picked, which is what NVS keeps. The config holds
// the size in effect: the picked one, or the model's default while the model
// cannot take it. Going M220 -> M110 -> M220 lost a picked 50 x 30 for good
// before this (30.09.2026): the M110 took 40 x 30 and that was saved.
static uint16_t s_want_w = 0, s_want_l = 0;

const LabelPrinterProfile& labelPrinterProfile(LabelPrinterModel model) {
  switch (model) {
    case LP_MODEL_M220: return PROFILE_M220;
    case LP_MODEL_M110: return PROFILE_M110;
    case LP_MODEL_M110S: return PROFILE_M110S;
    case LP_MODEL_M100: return PROFILE_M100;
    case LP_MODEL_M120: return PROFILE_M120;
    case LP_MODEL_M200: return PROFILE_M200;
    case LP_MODEL_M221: return PROFILE_M221;
    case LP_MODEL_NIIMBOT_B:  return PROFILE_NIIMBOT_B;
    case LP_MODEL_NIIMBOT_M2: return PROFILE_NIIMBOT_M2;
    case LP_MODEL_BROTHER_QL820: return PROFILE_BROTHER_QL820;
    default:            return PROFILE_NONE;
  }
}

static bool isBrother(const LabelPrinterProfile& p) {
  return p.model != LP_MODEL_NONE && p.protocol == LP_PROTO_BROTHER_QL;
}

// The Brother's label table entry for a size in mm; nullptr for one it lacks.
// The table counts in bytes, so a size beyond one is none, never a wrapped one.
static const BrotherQlLabel* brotherLabel(uint16_t width_mm, uint16_t length_mm) {
  if (width_mm > 0xFF || length_mm > 0xFF) return nullptr;
  return brotherQlLabel((uint8_t)width_mm, (uint8_t)length_mm);
}

static bool mediaFits(const LabelPrinterProfile& p, uint16_t w, uint16_t h) {
  // What a DK label prints is its own entry, not a span of mm (design L1,
  // ruling 4): 62 x 29 is 696 x 271 dots, not the 732 x 343 of its size.
  if (isBrother(p)) return brotherLabel(w, h) != nullptr;
  return p.model != LP_MODEL_NONE &&
         w >= p.min_width_mm && w <= p.max_width_mm &&
         h >= p.min_length_mm && h <= p.max_length_mm;
}

bool labelPrinterTakesMedia(LabelPrinterModel model, uint16_t width_mm, uint16_t length_mm) {
  return mediaFits(labelPrinterProfile(model), width_mm, length_mm);
}

// What is stored is taken as it is, except for two things that would leave
// the printer unusable: no model becomes the M220, and a label size the model
// cannot take becomes the picked one where it fits, else the model's default.
static LabelPrinterConfig normalized(const LabelPrinterConfig& in) {
  LabelPrinterConfig c = in;
  const LabelPrinterProfile& p = labelPrinterProfile(c.model);
  const LabelPrinterProfile& use = p.model == LP_MODEL_NONE ? PROFILE_M220 : p;
  c.model = use.model;
  c.name[sizeof(c.name) - 1] = '\0';
  c.address[sizeof(c.address) - 1] = '\0';
  if (c.x_offset > LP_OFFSET_RIGHT) c.x_offset = LP_OFFSET_RIGHT;
  if (c.x_offset < LP_OFFSET_LEFT)  c.x_offset = LP_OFFSET_LEFT;
  if (mediaFits(use, s_want_w, s_want_l)) {
    c.media_width_mm  = s_want_w;
    c.media_length_mm = s_want_l;
  } else if (!mediaFits(use, c.media_width_mm, c.media_length_mm)) {
    c.media_width_mm  = use.default_width_mm;
    c.media_length_mm = use.default_length_mm;
  }
  return c;
}

static void logConfig(const LabelPrinterConfig& c, bool ok) {
  logSDf("Printer: config %s %s '%s' %ux%u mm offset %d (%d) %s",
         labelPrinterProfile(c.model).name, c.address[0] ? c.address : "-", c.name,
         (unsigned)c.media_width_mm, (unsigned)c.media_length_mm, (int)c.x_offset,
         (int)labelPrinterOffset(c), ok ? "saved" : "NOT saved");
}

// ---- The Bluetooth printer ----------------------------------------------

static LabelPrinterConfig bleConfig() {
  if (s_loaded) return s_config;
  LabelPrinterConfig c{};
  c.model = (LabelPrinterModel)prefsGetInt(LP_KEY_MODEL, LP_MODEL_M220);
  snprintf(c.name, sizeof(c.name), "%s", prefsGetString(LP_KEY_NAME, "").c_str());
  snprintf(c.address, sizeof(c.address), "%s", prefsGetString(LP_KEY_ADDR, "").c_str());
  c.media_width_mm  = prefsGetInt(LP_KEY_W, PROFILE_M220.default_width_mm);
  c.media_length_mm = prefsGetInt(LP_KEY_H, PROFILE_M220.default_length_mm);
  c.x_offset = loadOffset(c.address);
  s_want_w = c.media_width_mm;
  s_want_l = c.media_length_mm;
  s_config = normalized(c);
  s_loaded = true;
  return s_config;
}

static bool bleSave(const LabelPrinterConfig& in) {
  const LabelPrinterConfig before = bleConfig();
  // A size other than the one in effect is a new pick; the same size with
  // another model is not, and leaves the picked one as it was.
  const uint16_t want_w = s_want_w, want_l = s_want_l;
  if (in.media_width_mm != before.media_width_mm ||
      in.media_length_mm != before.media_length_mm) {
    s_want_w = in.media_width_mm;
    s_want_l = in.media_length_mm;
  }
  LabelPrinterConfig c = normalized(in);
  // Another device is another printer: its own offset, whatever the caller
  // carried over from the one before.
  if (strcmp(in.address, before.address) != 0) c.x_offset = loadOffset(c.address);
  bool ok = true;
  ok = prefsPutInt(LP_KEY_MODEL, (int)c.model) && ok;
  ok = prefsPutString(LP_KEY_ADDR, c.address) && ok;
  ok = prefsPutString(LP_KEY_NAME, c.name) && ok;
  ok = prefsPutInt(LP_KEY_W, s_want_w) && ok;
  ok = prefsPutInt(LP_KEY_H, s_want_l) && ok;
  char key[20];
  if (offsetKey(c.address, key, sizeof(key))) ok = prefsPutInt(key, c.x_offset) && ok;
  if (ok) { s_config = c; s_loaded = true; }
  else { s_want_w = want_w; s_want_l = want_l; }
  logConfig(c, ok);
  return ok;
}

// ---- The Brother on the network -----------------------------------------

// What the Brother's address may be: none, or four numbers as a person types
// them. A Bluetooth address never passes, so it never reaches the socket.
static bool hostAccepted(const char* host) {
  return host && (!host[0] || brotherQlHostValid(host));
}

// The Brother as the config the rest of the code knows: its model, its IP
// address as the device, no name, its label and its own offset. Only the
// address and the offset are taken from `in`: the model is the one Brother
// profile there is (the save refuses any other), and the label is the
// table's whatever a caller sends. There is one label so far, so it is not
// stored: a second one gets a key of its own.
static LabelPrinterConfig netNormalized(const LabelPrinterConfig& in) {
  LabelPrinterConfig c{};
  c.model = LP_MODEL_BROTHER_QL820;
  memcpy(c.address, in.address, sizeof(c.address));
  c.address[sizeof(c.address) - 1] = '\0';
  c.media_width_mm  = BROTHER_QL_LABELS[0].width_mm;
  c.media_length_mm = BROTHER_QL_LABELS[0].length_mm;
  c.x_offset = in.x_offset;
  if (c.x_offset > LP_OFFSET_RIGHT) c.x_offset = LP_OFFSET_RIGHT;
  if (c.x_offset < LP_OFFSET_LEFT)  c.x_offset = LP_OFFSET_LEFT;
  return c;
}

static LabelPrinterConfig netConfig() {
  if (s_net_loaded) return s_net;
  LabelPrinterConfig c{};
  // Taken only as the check passes it: a key written by anything else never
  // ends up as the address the socket is opened to.
  const String host = prefsGetString(LP_KEY_HOST, "");
  if (hostAccepted(host.c_str())) snprintf(c.address, sizeof(c.address), "%s", host.c_str());
  c.x_offset = (int16_t)prefsGetInt(LP_KEY_NET_XOFF, 0);
  s_net = netNormalized(c);
  s_net_loaded = true;
  return s_net;
}

// The address through the same check as labelPrinterSetHost(), and the
// offset. Nothing else is the Brother's to write, and no Bluetooth key is
// touched.
static bool netSave(const LabelPrinterConfig& in) {
  const LabelPrinterConfig c = netNormalized(in);
  if (!hostAccepted(c.address)) {
    logSDf("Printer: config %s address '%s' is no IPv4 address, NOT saved",
           PROFILE_BROTHER_QL820.name, c.address);
    return false;
  }
  bool ok = true;
  ok = prefsPutString(LP_KEY_HOST, c.address) && ok;
  ok = prefsPutInt(LP_KEY_NET_XOFF, c.x_offset) && ok;
  if (ok) { s_net = c; s_net_loaded = true; }
  logConfig(c, ok);
  return ok;
}

bool labelPrinterSetHost(const char* host) {
  if (!hostAccepted(host)) {
    logSDf("Printer: Brother address '%s' is no IPv4 address, NOT saved", host ? host : "");
    return false;
  }
  LabelPrinterConfig c = netConfig();
  const bool ok = prefsPutString(LP_KEY_HOST, host);
  if (ok) {
    snprintf(c.address, sizeof(c.address), "%s", host);
    s_net = c;
  }
  logSDf("Printer: Brother address %s %s", host[0] ? host : "-", ok ? "saved" : "NOT saved");
  return ok;
}

// ---- The chosen one -------------------------------------------------------

LabelPrinterLink labelPrinterLink() {
  if (!s_link_loaded) {
    s_link_stored = prefsGetInt(LP_KEY_LINK, -1);
    s_link_loaded = true;
  }
  // Only the key is kept, the switch is asked each time: until a link is
  // stored the switch decides, and it changes elsewhere too (the Bluetooth
  // screen, /api/ble). Keeping the derived link would go stale on that.
  return labelPrinterLinkFrom(s_link_stored, bleEnabled());
}

static const char* linkName(LabelPrinterLink link) {
  return link == LP_LINK_WLAN ? "wlan" : link == LP_LINK_BLE ? "bluetooth" : "off";
}

bool labelPrinterSetLink(LabelPrinterLink link) {
  if (link > LP_LINK_WLAN) return false;   // not a link; nothing written
  const bool ok = prefsPutInt(LP_KEY_LINK, (int)link);
  if (ok) {
    s_link_stored = link;
    s_link_loaded = true;
    // Bluetooth runs for the Bluetooth printer only. Switched only once the
    // choice is stored, so a failed write leaves the old choice whole.
    if (link == LP_LINK_BLE) bleSetEnabled(true);
    else if (bleEnabled()) bleSetEnabled(false);
  }
  logSDf("Printer: link -> %s %s", linkName(link), ok ? "saved" : "NOT saved");
  return ok;
}

LabelPrinterConfig labelPrinterLoadConfig() {
  return labelPrinterLink() == LP_LINK_WLAN ? netConfig() : bleConfig();
}

// Root: a screen or a page may still hold a copy of the printer from before
//   the link changed (the offset screen open while the browser switches),
//   and saving it went to the keys of the printer chosen now.
// Fix: a config whose model belongs to the other link is refused and nothing
//   is written - a Brother while the link is Bluetooth or off, anything else
//   while it is WLAN (ruling R7).
// NOT-TO-DO: mapping it onto the chosen printer, as a Brother model once
//   became an M220: that wrote the stale copy's model and IP address over
//   the Bluetooth printer's.
bool labelPrinterSaveConfig(const LabelPrinterConfig& in) {
  const LabelPrinterLink link = labelPrinterLink();
  const LabelPrinterProfile& p = labelPrinterProfile(in.model);
  if (isBrother(p) != (link == LP_LINK_WLAN)) {
    logSDf("Printer: config for another link NOT saved (%s, link %s)",
           p.name ? p.name : "no model", linkName(link));
    return false;
  }
  return link == LP_LINK_WLAN ? netSave(in) : bleSave(in);
}

bool labelPrinterConfigured(const LabelPrinterConfig& c) { return c.address[0] != '\0'; }

bool labelPrinterRecommended(LabelPrinterModel model) {
  return model == LP_MODEL_M220 || model == LP_MODEL_NIIMBOT_M2;
}

// Root: forgetting followed the link at the moment of the call, so a
//   Bluetooth caller - the device card's Remove, the printer screen reached
//   from the print sheet, a browser page still showing the Bluetooth tile -
//   emptied the Brother's address once the link had been switched to WLAN.
// Fix: always the Bluetooth device, whatever link is chosen. Its cache is
//   updated by the save; the Brother's is not touched, so under WLAN the
//   chosen config stays the Brother's.
// NOT-TO-DO: refusing under WLAN: the card's Remove is meant to drop the
//   Bluetooth printer there too. The Brother's address goes through
//   labelPrinterSetHost("") only.
bool labelPrinterForget() {
  LabelPrinterConfig c = bleConfig();
  c.address[0] = '\0';
  c.name[0] = '\0';
  return bleSave(c);
}

bool labelPrinterIsDevice(const char* address) {
  if (!address || !address[0]) return false;
  const LabelPrinterConfig c = labelPrinterLoadConfig();
  return c.address[0] && strcmp(c.address, address) == 0;
}

// As a ratio so the rounding stays exact: at 203 dpi this is the 8 dots to
// the millimetre the M220 was measured with, bit for bit.
uint16_t labelPrinterDotsForMm(LabelPrinterModel model, uint16_t mm) {
  const uint32_t dpi = labelPrinterProfile(model).dpi;
  return (uint32_t(mm) * dpi * 10 + 127) / 254;
}

uint16_t labelPrinterRasterWidth(LabelPrinterModel model, uint16_t media_width_mm) {
  const LabelPrinterProfile& p = labelPrinterProfile(model);
  if (p.model == LP_MODEL_NONE) return 0;
  const uint16_t content = labelPrinterDotsForMm(model, media_width_mm);
  const uint16_t canvas = content > p.base_raster_width ? content : p.base_raster_width;
  const uint16_t width = (canvas + 7) & ~uint16_t(7);
  return width > p.max_raster_width ? p.max_raster_width : width;
}

// Every Phomemo head is at least as wide as its widest stock, so there this
// is the plain conversion; a 50 mm roll on a 48 mm head prints 48 mm of it.
// A Brother leaves a margin on each side of a DK label: its table says what
// is printable, 696 of the 732 dots that 62 mm come to (befunde 2).
static uint16_t brotherWidth(uint16_t width_mm) {
  return width_mm > 0xFF ? 0 : brotherQlPrintWidth((uint8_t)width_mm);
}

uint16_t labelPrinterContentWidth(LabelPrinterModel model, uint16_t media_width_mm) {
  const uint16_t dots = isBrother(labelPrinterProfile(model))
      ? brotherWidth(media_width_mm) : labelPrinterDotsForMm(model, media_width_mm);
  const uint16_t row = labelPrinterRasterWidth(model, media_width_mm);
  return dots > row ? row : dots;
}

// Along the feed the same: a die-cut 29 mm label prints 271 of its 343 lines.
uint16_t labelPrinterContentHeight(LabelPrinterModel model, uint16_t width_mm,
                                   uint16_t length_mm) {
  if (!isBrother(labelPrinterProfile(model))) return labelPrinterDotsForMm(model, length_mm);
  const BrotherQlLabel* label = brotherLabel(width_mm, length_mm);
  return label ? label->dots_h : 0;
}

// A ruler across the M220's 576 dots put Nikolai's 40 mm roll under dots 128
// to 448, the middle (24.09.2026); a user's roll sat 16 mm further right, at
// the fixed wall of the roll holder (30.09.2026). Where the roll runs is how
// it sits in the holder, not the model, so it is a setting of its own.
void labelPrinterOffsetRange(const LabelPrinterConfig& c, int16_t* min, int16_t* max) {
  const uint16_t row = labelPrinterRasterWidth(c.model, c.media_width_mm);
  const uint16_t content = labelPrinterContentWidth(c.model, c.media_width_mm);
  const int16_t slack = content < row ? int16_t(row - content) : 0;
  const int16_t centre = slack / 2;
  if (min) *min = -centre;
  if (max) *max = slack - centre;
}

int16_t labelPrinterOffset(const LabelPrinterConfig& c) {
  int16_t lo, hi;
  labelPrinterOffsetRange(c, &lo, &hi);
  return c.x_offset < lo ? lo : c.x_offset > hi ? hi : c.x_offset;
}

uint16_t labelPrinterContentX(const LabelPrinterConfig& c) {
  int16_t lo;
  labelPrinterOffsetRange(c, &lo, nullptr);
  return uint16_t(labelPrinterOffset(c) - lo);
}

// The renderer works in whole dots from the same conversion, so the content
// matches exactly; one dot of slack is left for a raster from elsewhere.
static bool nearDots(uint16_t px, uint16_t d) {
  return px + 1 >= d && px <= d + 1;
}

bool labelPrinterRasterFits(LabelPrinterModel model, const LabelRaster& image,
                            uint16_t media_width_mm, uint16_t media_length_mm) {
  const LabelPrinterProfile& p = labelPrinterProfile(model);
  return mediaFits(p, media_width_mm, media_length_mm) &&
         image.width == labelPrinterRasterWidth(model, media_width_mm) &&
         nearDots(image.content_width, labelPrinterContentWidth(model, media_width_mm)) &&
         nearDots(image.height, labelPrinterContentHeight(model, media_width_mm, media_length_mm));
}

// What the transport says, in the printer's terms; both drivers share it.
static LabelPrintResult fromBle(BleWriteResult r) {
  switch (r) {
    case BLE_WRITE_OK:                return LP_OK;
    case BLE_WRITE_OFF:               return LP_BLE_OFF;
    case BLE_WRITE_INIT_FAILED:       return LP_BLE_INIT;
    case BLE_WRITE_CONNECT_FAILED:    return LP_BLE_CONNECT;
    case BLE_WRITE_NO_CHARACTERISTIC: return LP_BLE_CHARACTERISTIC;
    case BLE_WRITE_STUCK:             return LP_BLE_STUCK;
    case BLE_WRITE_SENT_UNCONFIRMED:  return LP_SENT_UNCONFIRMED;
    default:                          return LP_BLE_WRITE;
  }
}

static LabelPrintResult printNiimbot(const LabelPrinterProfile& p, const LabelPrinterConfig& c,
                                     const LabelRaster& image, BleProgressFn progress) {
  NiimbotJobInfo info{};
  const NiimbotClass cls = p.model == LP_MODEL_NIIMBOT_M2 ? NB_CLASS_M2 : NB_CLASS_B;
  const NiimbotResult r = niimbotPrint(c.address, image, cls, progress, &info);
  logSDf("Printer: NIIMBOT %s result=%d model=0x%04X transport=%d", p.name, (int)r,
         (unsigned)info.model_id, (int)info.transport);
  switch (r) {
    case NB_OK:             return LP_OK;
    // A transport failure that left no reason is still a failure, never "ok".
    case NB_TRANSPORT:      return info.transport == BLE_WRITE_NO_CHARACTERISTIC ? LP_BLE_NOT_NIIMBOT
                                 : info.transport == BLE_WRITE_OK ? LP_BLE_WRITE
                                 : fromBle(info.transport);
    // Silent from the start: not a NIIMBOT. Silent after it had answered: the
    // printer dropped out mid-job.
    case NB_NO_REPLY:       return info.packets_rx ? LP_BLE_WRITE : LP_BLE_NOT_NIIMBOT;
    case NB_MODEL_UNKNOWN:  return LP_MODEL_UNKNOWN;
    case NB_MODEL_MISMATCH: return LP_MODEL_MISMATCH;
    case NB_PRN_COVER:      return LP_PRN_COVER;
    case NB_PRN_NO_PAPER:   return LP_PRN_NO_PAPER;
    case NB_PRN_NO_RIBBON:  return LP_PRN_NO_RIBBON;
    case NB_PRN_BUSY:       return LP_PRN_BUSY;
    case NB_UNCONFIRMED:    return LP_SENT_UNCONFIRMED;
    default:                return LP_PRN_ERROR;   // rejected, or another error code
  }
}

// What the Brother's driver says, in the printer's terms. "Sent" is all a
// Brother on its raw port can tell (befunde 3), so it is a result of its own
// and never LP_OK: the card says not to print again.
static LabelPrintResult fromBrother(BrotherQlResult r) {
  switch (r) {
    case BQ_SENT:       return LP_NET_SENT;
    case BQ_BAD_RASTER: return LP_BAD_RASTER;
    case BQ_NO_WIFI:    return LP_NET_NO_WIFI;
    case BQ_CONNECT:    return LP_NET_CONNECT;
    case BQ_WRITE:
    default:            return LP_NET_ABORT;
  }
}

static LabelPrintResult printBrother(const LabelPrinterConfig& c, const LabelRaster& image,
                                     BleProgressFn progress) {
  // labelPrinterRasterFits() has made sure the table has this size.
  const BrotherQlLabel* label = brotherLabel(c.media_width_mm, c.media_length_mm);
  if (!label) return LP_MEDIA_MISMATCH;
  return fromBrother(brotherQlPrint(c.address, *label, image, progress));
}

LabelPrintResult labelPrinterPrint(const LabelPrinterConfig& c, const LabelRaster& image,
                                   BleProgressFn progress) {
  const LabelPrinterProfile& p = labelPrinterProfile(c.model);
  if (p.model == LP_MODEL_NONE || !labelPrinterConfigured(c)) return LP_NO_PRINTER;
  if (!labelRasterValid(image)) return LP_BAD_RASTER;
  if (image.width > p.max_raster_width) return LP_TOO_WIDE;
  if (!labelPrinterRasterFits(c.model, image, c.media_width_mm, c.media_length_mm))
    return LP_MEDIA_MISMATCH;
  if (p.protocol == LP_PROTO_NIIMBOT) return printNiimbot(p, c, image, progress);
  if (p.protocol == LP_PROTO_BROTHER_QL) return printBrother(c, image, progress);
  const PhomemoModel pm = p.protocol == LP_PROTO_PHOMEMO_M220 ? PHOMEMO_M220 : PHOMEMO_M110;
  return fromBle(phomemoMSeriesPrint(pm, c.address, image, progress));
}

int labelPrintResultString(LabelPrintResult r) {
  switch (r) {
    case LP_OK:                 return STR_PRN_OK;
    case LP_NO_PRINTER:         return STR_PRN_ERR_NO_PRINTER;
    case LP_BAD_RASTER:         return STR_PRN_ERR_RASTER;
    case LP_TOO_WIDE:           return STR_PRN_ERR_TOO_WIDE;
    case LP_MEDIA_MISMATCH:     return STR_PRN_ERR_MEDIA;
    case LP_BLE_OFF:            return STR_PRN_ERR_BLE_OFF;
    // The stack does not start for two reasons: no memory, or the memory it
    // needs went back to the heap at boot (see bleStackAvailable()).
    case LP_BLE_INIT:           return bleStackAvailable() ? STR_BT_INIT_FAILED
                                                           : STR_PRN_ERR_BLE_RESTART;
    case LP_BLE_CONNECT:        return STR_PRN_ERR_CONNECT;
    case LP_BLE_CHARACTERISTIC: return STR_PRN_ERR_NOT_PRINTER;
    case LP_BLE_STUCK:          return STR_PRN_ERR_STUCK;
    case LP_SENT_UNCONFIRMED:   return STR_PRN_UNCONF_MSG;
    case LP_BLE_NOT_NIIMBOT:    return STR_PRN_ERR_NOT_NIIMBOT;
    case LP_PRN_COVER:          return STR_PRN_ERR_COVER;
    case LP_PRN_NO_PAPER:       return STR_PRN_ERR_NO_PAPER;
    case LP_PRN_NO_RIBBON:      return STR_PRN_ERR_NO_RIBBON;
    case LP_PRN_BUSY:           return STR_PRN_ERR_BUSY;
    case LP_PRN_ERROR:          return STR_PRN_ERR_PRINTER;
    case LP_MODEL_MISMATCH:     return STR_PRN_ERR_MODEL_MISMATCH;
    case LP_MODEL_UNKNOWN:      return STR_PRN_ERR_MODEL_UNKNOWN;
    case LP_SERVER_LABEL:       return STR_PRN_ERR_SERVER_LABEL;
    // The Brother: "sent" comes with what to check when no label comes out.
    case LP_NET_SENT:           return STR_PRN_NET_SENT_MSG;
    case LP_NET_NO_WIFI:        return STR_NO_WIFI;
    case LP_NET_CONNECT:        return STR_PRN_ERR_NET_CONNECT;
    case LP_NET_ABORT:          return STR_PRN_ERR_NET_ABORT;
    case LP_PRINTER_OFF:        return STR_PRN_ERR_LINK_OFF;
    case LP_NET_REPLY:          return STR_PRN_NET_REPLY_MSG;
    case LP_NET_BROKEN:         return STR_PRN_NET_BROKEN_MSG;
    default:                    return STR_PRN_ERR_WRITE;
  }
}

// Design L1, ruling 9: a print may start when the Bluetooth printer is chosen,
// Bluetooth runs and a device is picked, or when the Brother is chosen and has
// an address. Whether the scale is on WLAN is the print's to say: the Brother
// answers that with LP_NET_NO_WIFI.
LabelPrintResult labelPrinterReadiness() {
  const LabelPrinterLink link = labelPrinterLink();
  if (link == LP_LINK_OFF) return LP_PRINTER_OFF;
  if (!labelPrinterConfigured(labelPrinterLoadConfig())) return LP_NO_PRINTER;
  if (link == LP_LINK_WLAN) return LP_OK;
  // Bluetooth is on, that is what makes the link Bluetooth; but switched on
  // in this boot it waits for the restart (see bleStackAvailable()).
  if (!bleStackAvailable()) return LP_BLE_INIT;
  if (bleStackStuck()) return LP_BLE_STUCK;
  return LP_OK;
}

BleSessionPhase labelPrinterSessionPhase() {
  return labelPrinterLink() == LP_LINK_WLAN ? brotherQlPhase() : bleSessionPhase();
}

void labelPrinterSessionBytes(size_t* sent, size_t* total) {
  if (labelPrinterLink() == LP_LINK_WLAN) brotherQlBytes(sent, total);
  else bleSessionBytes(sent, total);
}

const LabelPrinterModel* labelPrinterModels(LabelPrinterLink link, int* count) {
  if (link == LP_LINK_WLAN) {
    if (count) *count = LABEL_PRINTER_MODEL_WLAN_COUNT;
    return LABEL_PRINTER_MODELS_WLAN;
  }
  if (count) *count = LABEL_PRINTER_MODEL_COUNT;
  return LABEL_PRINTER_MODELS;
}

// The Brother's sizes, copied from its label table on first use: that table
// (brother_ql_rules.cpp) is the one place a DK label is added. A constant
// list here would be a second table to keep in step; the table's count is no
// constant in this file, hence the room for a few.
#define LP_NET_SIZES_MAX 8
static LabelMediaSize s_net_sizes[LP_NET_SIZES_MAX];
static int s_net_size_count = -1;

const LabelMediaSize* labelPrinterMediaSizes(LabelPrinterModel model, int* count) {
  if (!isBrother(labelPrinterProfile(model))) {
    if (count) *count = LABEL_MEDIA_SIZE_COUNT;
    return LABEL_MEDIA_SIZES;
  }
  if (s_net_size_count < 0) {
    int n = 0;
    for (int i = 0; i < BROTHER_QL_LABEL_COUNT && n < LP_NET_SIZES_MAX; i++)
      s_net_sizes[n++] = LabelMediaSize{ BROTHER_QL_LABELS[i].width_mm,
                                         BROTHER_QL_LABELS[i].length_mm };
    s_net_size_count = n;
  }
  if (count) *count = s_net_size_count;
  return s_net_sizes;
}

// The next stock size the model can take, after the current one; the first
// fitting one when the current one is not in the model's list.
void labelPrinterNextMedia(LabelPrinterConfig& c) {
  const LabelPrinterProfile& p = labelPrinterProfile(c.model);
  int count = 0;
  const LabelMediaSize* sizes = labelPrinterMediaSizes(c.model, &count);
  int at = -1;
  for (int i = 0; i < count; i++)
    if (sizes[i].width_mm == c.media_width_mm &&
        sizes[i].length_mm == c.media_length_mm) { at = i; break; }
  for (int step = 1; step <= count; step++) {
    const LabelMediaSize& s = sizes[(at + step) % count];
    if (mediaFits(p, s.width_mm, s.length_mm)) {
      c.media_width_mm = s.width_mm;
      c.media_length_mm = s.length_mm;
      return;
    }
  }
}

bool labelPrinterAskFirst() {
  if (!s_ask_loaded) { s_ask_first = prefsGetBool(LP_KEY_ASK, true); s_ask_loaded = true; }
  return s_ask_first;
}

bool labelPrinterSetAskFirst(bool ask) {
  const bool ok = prefsPutBool(LP_KEY_ASK, ask);
  if (ok) { s_ask_first = ask; s_ask_loaded = true; }
  logSDf("Printer: print sheet %s %s", ask ? "on" : "off", ok ? "saved" : "NOT saved");
  return ok;
}
