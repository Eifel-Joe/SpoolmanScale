"""Whether the Bluetooth label printer was there (Spec 5.3 AC f21, V74; the idea from akira69's fork, built anew): the Bluetooth symbol in the header turns green when the printer set up was found by the last scan or took the last print, a printer's own complaint ("no paper", "cover open") included, since it answered; grey when it could not be reached then, after a start and after another printer is picked. Only the Bluetooth printer: the Brother over WLAN gets no symbol. Kept in memory only.

The rules are compiled; the wiring (print, scan, a new device, the header, the acceptance build's made-up answer) is read from the source."""
import subprocess
import tempfile
from pathlib import Path

import klipper_src as ks

root = Path(__file__).parents[1]
ks.assert_ci_step("test_printer_reach.py")

source = r'''
#include "services/label_printer_reach.h"
#include <cassert>
#include <cstring>
#include <initializer_list>

static BleDevice dev(const char* address) {
  BleDevice d{};
  strncpy(d.address, address, sizeof(d.address) - 1);
  return d;
}

int main() {
  // The printer answered: green, its complaints included.
  for (LabelPrintResult r : { LP_OK, LP_SENT_UNCONFIRMED, LP_PRN_COVER, LP_PRN_NO_PAPER,
                              LP_PRN_NO_RIBBON, LP_PRN_BUSY, LP_PRN_ERROR, LP_MODEL_MISMATCH,
                              LP_MODEL_UNKNOWN })
    assert(labelPrintReach(r) == LP_REACH_YES);
  // Not reached, or not the printer: grey.
  for (LabelPrintResult r : { LP_BLE_CONNECT, LP_BLE_OFF, LP_BLE_INIT, LP_BLE_STUCK, LP_BLE_WRITE,
                              LP_BLE_CHARACTERISTIC, LP_BLE_NOT_NIIMBOT })
    assert(labelPrintReach(r) == LP_REACH_NO);
  // Nothing went on the air, or not over Bluetooth: as it was.
  for (LabelPrintResult r : { LP_NO_PRINTER, LP_BAD_RASTER, LP_TOO_WIDE, LP_MEDIA_MISMATCH,
                              LP_SERVER_LABEL, LP_PRINTER_OFF, LP_NET_SENT, LP_NET_NO_WIFI,
                              LP_NET_CONNECT, LP_NET_ABORT, LP_NET_REPLY, LP_NET_BROKEN })
    assert(labelPrintReach(r) == LP_REACH_KEEP);

  // A scan: the printer's address among the devices found, in either case.
  const BleDevice found[] = { dev("11:22:33:44:55:66"), dev("aa:bb:cc:dd:ee:ff") };
  assert(labelScanReach("AA:BB:CC:DD:EE:FF", found, 2) == LP_REACH_YES);
  assert(labelScanReach("aa:bb:cc:dd:ee:ff", found, 2) == LP_REACH_YES);
  assert(labelScanReach("aa:bb:cc:dd:ee:00", found, 2) == LP_REACH_NO);
  assert(labelScanReach("aa:bb:cc:dd:ee:ff", found, 0) == LP_REACH_NO);
  assert(labelScanReach("aa:bb:cc:dd:ee:ff", found, 1) == LP_REACH_NO);
  // No printer set up: nothing to be green about.
  assert(labelScanReach("", found, 2) == LP_REACH_NO);
  assert(labelScanReach(nullptr, found, 2) == LP_REACH_NO);
  // The scan itself failed (the stack did not start): it says nothing.
  assert(labelScanReach("aa:bb:cc:dd:ee:ff", nullptr, -1) == LP_REACH_KEEP);
  return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "test.cpp"
    cpp.write_text(source)
    binary = Path(tmp) / "test"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"), str(cpp),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

problems = []


def need(ok, text):
    if not ok:
        problems.append(text)


LP = "services/label_printer.cpp"

# 1. A print over Bluetooth leaves its verdict; the Brother's never does.
pr = ks.squeeze(ks.body_of(LP, "labelPrinterPrint"))
need("labelPrintReach(" in pr, f"src/{LP}: labelPrinterPrint() does not note whether the printer was reached")
brother = pr.find("LP_PROTO_BROTHER_QL")
reach = pr.find("labelPrintReach(")
need(brother >= 0 and reach > brother,
     f"src/{LP}: labelPrinterPrint() notes the reach before the Brother has gone its own way")

# 2. Another device is another printer: grey until it is found or prints.
save = ks.squeeze(ks.body_of(LP, "bleSave"))
need("LP_REACH_NO" in save, f"src/{LP}: bleSave() leaves a new device as green as the old one")

# 3. A scan says whether the printer set up was among the devices.
scan = ks.squeeze(ks.body_of(LP, "labelPrinterNoteScan"))
need("labelScanReach(" in scan, f"src/{LP}: labelPrinterNoteScan() does not apply the scan rule")
screen = ks.squeeze(ks.body_of("ui/ble_devices_screen.cpp", "handleBleDevicesDeferredActions"))
need("labelPrinterNoteScan(" in screen,
     "src/ui/ble_devices_screen.cpp: the scan does not tell the label printer what it found")
i_scan, i_note = screen.find("bleScan("), screen.find("labelPrinterNoteScan(")
need(i_scan >= 0 and i_note > i_scan,
     "src/ui/ble_devices_screen.cpp: the scan result is not noted after the scan")

# 4. The header: green while reached, the caption grey otherwise, Bluetooth only.
hdr = ks.squeeze(ks.body_of("ui/header_status.cpp", "updateHeaderStatus"))
need("labelPrinterReachable()" in hdr and "UI_COL_GOOD" in hdr and "UI_COL_CAPTION" in hdr,
     "src/ui/header_status.cpp: the Bluetooth symbol does not show the printer's reach")

# 5. The acceptance build's made-up answer (Spec 5.3 AA, V74): asked before
#    any Bluetooth driver, never on the Brother's way, and for a scan; a
#    release gets stubs that answer "none".
ble = ks.squeeze(ks.body_of(LP, "printBle"))
i_sim = ble.find("docPrinterPrintTake()")
drivers = [ble.find(d) for d in ("printNiimbot(", "tsplPrint(", "phomemoMSeriesPrint(")]
need(i_sim >= 0 and all(d > i_sim for d in drivers),
     f"src/{LP}: printBle() does not ask for a made-up answer before every Bluetooth driver")
need("docPrinterPrintTake" not in ks.squeeze(ks.body_of(LP, "printBrother")),
     f"src/{LP}: the Brother's print asks for a made-up answer")
need("docPrinterScanTake()" in scan, f"src/{LP}: labelPrinterNoteScan() does not ask for a made-up scan")
doc_h = (ks.src / "app/doc_build.h").read_text(encoding="utf-8")
split = doc_h.find("\n#else")   # the branch, not the word in a comment
need(split > 0 and "int docPrinterPrintTake();" in doc_h[:split] and "int docPrinterScanTake();" in doc_h[:split],
     "src/app/doc_build.h: the acceptance build does not declare the made-up printer answers")
need("docPrinterPrintTake() { return -1; }" in doc_h[split:] and "docPrinterScanTake() { return -1; }" in doc_h[split:],
     "src/app/doc_build.h: a release does not answer 'none' for the made-up printer answers")
need("/api/sim/label-printer" in ks.source_of("web/web_doc.cpp").text,
     "src/web/web_doc.cpp: no route /api/sim/label-printer")

assert not problems, "\n" + "\n".join(f"  - {p}" for p in problems)
print("test_printer_reach: ok")
