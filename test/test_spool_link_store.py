"""Where the link a server names for a spool is kept (Spec 5.3 U3, V75): o18
came from the original with the link in internal RAM, three times 128 bytes
(the scan's, the label copy's and the copy kept for a failed server label).
The 7B has about 45 kB of internal RAM left in use, so the link lives in a
few slots in PSRAM instead, by spool id and by the server it came from.

Compiles the slot rules and checks them; reads the source for where the link
is written and read."""
import subprocess
import tempfile
from pathlib import Path

import klipper_src as ks

root = Path(__file__).parents[1]
ks.assert_ci_step("test_spool_link_store.py")

source = r"""
#include "services/spool_link_slots.h"
#include <cassert>
#include <cstring>
#include <string>

static SpoolLinkSlot s[SPOOL_LINK_SLOTS];
static int next_slot = 0;

static const char* at(int id, unsigned gen) {
  const int i = spoolLinkSlotFind(s, SPOOL_LINK_SLOTS, id, gen);
  return i < 0 ? nullptr : s[i].url;
}

int main() {
  static_assert(SPOOL_LINK_SLOTS >= 4 && SPOOL_LINK_SLOTS <= 16, "a few spools, no more");
  const char* a = "https://dev.spooly.eu/filament/e050d133";
  const char* b = "http://192.168.4.100:7913/spool/show/12";
  // Kept by id and by the server's generation.
  assert(spoolLinkSlotPut(s, SPOOL_LINK_SLOTS, &next_slot, 12, 1, a));
  assert(at(12, 1) && strcmp(at(12, 1), a) == 0);
  assert(!at(12, 2) && !at(13, 1));
  // A second read of the same spool replaces its link, in the same slot.
  assert(spoolLinkSlotPut(s, SPOOL_LINK_SLOTS, &next_slot, 12, 1, b));
  assert(strcmp(at(12, 1), b) == 0);
  int held = 0;
  for (const SpoolLinkSlot& x : s) held += x.id == 12;
  assert(held == 1);
  // A read without a link, or with one the rules refuse, drops the kept one.
  assert(!spoolLinkSlotPut(s, SPOOL_LINK_SLOTS, &next_slot, 12, 1, ""));
  assert(!at(12, 1));
  assert(spoolLinkSlotPut(s, SPOOL_LINK_SLOTS, &next_slot, 12, 1, a));
  assert(!spoolLinkSlotPut(s, SPOOL_LINK_SLOTS, &next_slot, 12, 1, "ftp://a.example/1"));
  assert(!at(12, 1));
  assert(spoolLinkSlotPut(s, SPOOL_LINK_SLOTS, &next_slot, 12, 1, a));
  assert(!spoolLinkSlotPut(s, SPOOL_LINK_SLOTS, &next_slot, 12, 1, nullptr));
  assert(!at(12, 1));
  // No spool, no link.
  assert(!spoolLinkSlotPut(s, SPOOL_LINK_SLOTS, &next_slot, 0, 1, a));
  assert(!spoolLinkSlotPut(s, SPOOL_LINK_SLOTS, &next_slot, -3, 1, a));
  assert(!at(0, 1));
  // More spools than slots: the oldest goes, the newest are all there.
  for (int id = 1; id <= SPOOL_LINK_SLOTS + 1; id++) {
    std::string u = "https://a.example/s/" + std::to_string(id);
    assert(spoolLinkSlotPut(s, SPOOL_LINK_SLOTS, &next_slot, id, 1, u.c_str()));
  }
  assert(!at(1, 1));
  for (int id = 2; id <= SPOOL_LINK_SLOTS + 1; id++) {
    std::string u = "https://a.example/s/" + std::to_string(id);
    assert(at(id, 1) && u == at(id, 1));
  }
  // Another server: what the old one named is gone, its slots are free again.
  for (int id = 2; id <= SPOOL_LINK_SLOTS + 1; id++) assert(!at(id, 2));
  for (int id = 100; id < 100 + SPOOL_LINK_SLOTS; id++) {
    std::string u = "https://b.example/" + std::to_string(id);
    assert(spoolLinkSlotPut(s, SPOOL_LINK_SLOTS, &next_slot, id, 2, u.c_str()));
  }
  for (int id = 100; id < 100 + SPOOL_LINK_SLOTS; id++) assert(at(id, 2));
  // Never longer than the room, never cut: one character too many is refused.
  std::string room = "https://a.example/" + std::string(SPOOL_LINK_LEN - 1 - 18, 'x');
  assert(spoolLinkSlotPut(s, SPOOL_LINK_SLOTS, &next_slot, 7, 2, room.c_str()));
  assert(room == at(7, 2));
  assert(!spoolLinkSlotPut(s, SPOOL_LINK_SLOTS, &next_slot, 7, 2, (room + "x").c_str()));
  assert(!at(7, 2));
  return 0;
}
"""

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

problems = []


def need(ok, text):
    if not ok:
        problems.append(text)


# 1. No 128-byte link in internal RAM: not in the scan's globals, not in the
#    label data that two statics and many stack frames hold.
for rel in ("app/app_state.h", "app/app_state.cpp"):
    need("sm_page_url" not in ks.code_of(rel), f"src/{rel}: the link still sits in sm_page_url")
render_h = ks.squeeze(ks.source_of("services/label_render.h").code)
start = render_h.find("structSpoolLabelData{")
struct = render_h[start:render_h.find("};", start)] if start >= 0 else ""
need(start >= 0 and "page_url" not in struct,
     "src/services/label_render.h: SpoolLabelData still carries page_url")

# 2. The slots are in PSRAM and shared under a lock: a worker task reads a
#    spool's dates (and its link) while the loop draws a label.
store = ks.squeeze(ks.source_of("services/spool_link_store.cpp").code)
need("MALLOC_CAP_SPIRAM" in store, "src/services/spool_link_store.cpp: the slots are not in PSRAM")
need("portENTER_CRITICAL" in store or "taskENTER_CRITICAL" in store,
     "src/services/spool_link_store.cpp: the slots are not under a lock")
need("backendGeneration()" in store,
     "src/services/spool_link_store.cpp: a link is not tied to the server it came from")

# 3. Every read of a spool on the Spoolman way puts its link, every way that
#    draws a spool label or shows its link asks for it.
writers = [
    ("ui/lookup_scan.cpp", "lookupResolveActive"),
    ("ui/spoolman_lookup.cpp", "querySpoolmanById"),
    ("app/label_spool.cpp", "fetchDates"),
]
for rel, fn in writers:
    body = ks.squeeze(ks.body_of(rel, fn))
    need("spoolLinkPut(" in body and "backendSpoolLink(" in body,
         f"src/{rel}: {fn}() does not keep the link the server names")
readers = [
    ("services/label_render.cpp", "labelRenderSpool", "spoolLinkCopy("),
    ("services/label_render.cpp", "labelQrForFilament", "spoolLinkHas("),
    ("web/pages/page_tags.cpp", "spoolJson", "spoolLinkCopy("),
]
for rel, fn, call in readers:
    need(call in ks.squeeze(ks.body_of(rel, fn)), f"src/{rel}: {fn}() does not ask for the spool's link")
for rel in ("app/label_spool.cpp", "ui/batch_create.cpp", "ui/printer_screen.cpp",
            "web/pages/page_label.cpp", "services/label_render.cpp"):
    need("page_url" not in ks.code_of(rel), f"src/{rel}: still copies a page_url of its own")

assert not problems, "\n" + "\n".join(f"  - {p}" for p in problems)
print("test_spool_link_store: ok")
