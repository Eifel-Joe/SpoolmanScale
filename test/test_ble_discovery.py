"""Exercise the firmware's bounded candidate selection with crowded RF input."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/ble_discovery.h"
#include <cassert>
#include <cstdio>

BleDevice device(int id, int rssi, const char* name = "") {
  BleDevice d{};
  snprintf(d.address, sizeof(d.address), "aa:bb:cc:dd:%02x:%02x", id / 256, id % 256);
  snprintf(d.name, sizeof(d.name), "%s", name);
  d.rssi = rssi;
  return d;
}
int main() {
  BleDevice records[32]{};
  auto printer = device(500, -95, "printer");
  BleCandidates scan(records, 32, printer.address, false);
  for (int i = 0; i < 100; ++i) scan.observe(device(i, -90));
  scan.observe(device(200, -25, "nearby"));
  assert(scan.count() == 32);
  assert(strcmp(records[0].name, "nearby") == 0);
  scan.observe(printer);
  assert(strcmp(records[0].name, "printer") == 0);
  assert(strcmp(records[1].name, "nearby") == 0);
  scan.observe(device(200, -40));
  assert(scan.count() == 32 && records[1].rssi == -40);
  assert(strcmp(records[1].name, "nearby") == 0);
  scan.observe(device(201, -30));
  assert(records[1].rssi == -30 && records[2].rssi == -40);

  BleDevice match{};
  BleCandidates target(&match, 1, printer.address, true);
  for (int i = 0; i < 100; ++i) target.observe(device(i, -20));
  assert(target.count() == 0);
  target.observe(printer);
  assert(target.count() == 1 && strcmp(match.address, printer.address) == 0);
  BleCandidates next(&match, 1, printer.address, true);
  assert(next.count() == 0);
  next.observe(printer);
  assert(next.count() == 1);
  BleCandidates empty(nullptr, 0, "", false);
  empty.observe(printer);
  assert(empty.count() == 0);
  auto page = bleCandidatePage(records, 32, true, 3, printer.address);
  assert(page.pages == 4 && page.number == 3 && page.count == 8);
  assert(page.indices[0] == 24 && page.indices[7] == 31);
  page = bleCandidatePage(records, 32, false, 3, printer.address);
  assert(page.pages == 1 && page.number == 0 && page.count == 2);
  assert(page.indices[0] == 0 && page.indices[1] == 2);
  page = bleCandidatePage(nullptr, 0, true, -1, "");
  assert(page.count == 0 && page.number == 0 && page.pages == 1);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "test.cpp"
    cpp.write_text(source)
    binary = Path(tmp) / "test"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"), str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("BLE crowded discovery tests passed")
