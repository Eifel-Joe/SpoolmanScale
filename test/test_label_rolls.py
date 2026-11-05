"""The Brother's rolls of v0.8.7-beta.7 (Spec 5.3 AC f08, V75), beyond the
bytes test_brother_ql.py checks: which arrangement and how many fact lines a
roll gets, that the 23 x 23 square carries only the material and the code,
that the Brother keeps its model, its roll and a print position for each
roll, and that the pages and the renderer go by the roll.

Compiles the layout header's roll rules and checks them; reads the source
for the rest."""
import subprocess
import tempfile
from pathlib import Path

import klipper_src as ks

root = Path(__file__).parents[1]
ks.assert_ci_step("test_label_rolls.py")

source = r"""
#include "services/label_layout.h"
#include <cassert>

int main() {
  // The shapes the fact counts are kept for. Continuous tape (length 0) has
  // no budget of its own: the label grows with its lines (Joerg, 05.11.2026).
  assert(labelRollShape(62, 29) == LRS_62 && labelRollShape(29, 62) == LRS_62);
  assert(labelRollShape(62, 0) == LRS_ENDLESS && labelRollShape(38, 0) == LRS_ENDLESS);
  assert(labelRollShape(23, 23) == LRS_SMALL);
  assert(labelMediaEndless(62, 0) && !labelMediaEndless(62, 29) && !labelMediaEndless(23, 23));
  // What there was stays: the Bluetooth rolls and a stock kept from earlier
  // betas such as 75 x 50 or 75 x 30, and the D110's tape.
  assert(labelRollShape(40, 30) == LRS_40 && labelRollShape(12, 40) == LRS_40);
  assert(labelRollShape(50, 30) == LRS_50 && labelRollShape(75, 50) == LRS_50);
  assert(labelRollShape(75, 30) == LRS_50);
  assert(labelMediaSmall(23, 23) && !labelMediaSmall(38, 40) && !labelMediaSmall(12, 40));

  // The square: the code alone, the standard arrangement, flat, whatever the
  // template says (Joerg, 05.11.2026: "nur fuer QR-Code geeignet"); the saved
  // template is untouched.
  LabelLayout t{};
  t.preset = LABEL_PRESET_COMPACT;
  t.fields = 0x1FFFF;
  t.options = (1u << LO_PORTRAIT) | (1u << LO_MATERIAL_PLAIN);
  const LabelLayout sq = labelLayoutForMedia(t, 23, 23);
  assert(sq.preset == LABEL_PRESET_STANDARD);
  assert(sq.fields == (1u << LF_QR));
  assert(!labelLayoutOption(sq, LO_PORTRAIT) && labelLayoutOption(sq, LO_MATERIAL_PLAIN));
  // Continuous tape: the template's fields and arrangement, always flat.
  const LabelLayout en = labelLayoutForMedia(t, 62, 0);
  assert(en.preset == t.preset && en.fields == t.fields && !labelLayoutOption(en, LO_PORTRAIT));
  assert(labelLayoutOption(en, LO_MATERIAL_PLAIN));
  // Every other roll takes the template as it is.
  static const uint16_t rolls[][2] = {{62, 29}, {40, 30}, {50, 30}, {12, 40}};
  for (const auto& wl : rolls) {
    const LabelLayout same = labelLayoutForMedia(t, wl[0], wl[1]);
    assert(same.preset == t.preset && same.fields == t.fields && same.options == t.options);
  }
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


LAYOUT = "services/label_layout.cpp"
LP = "services/label_printer.cpp"
RENDER = "services/label_render.cpp"

# 1. The fact counts go by the roll's shape: the square has none, continuous
#    tape takes every line a label can have (LABEL_MAX_FACTS).
cap = ks.squeeze(ks.body_of(LAYOUT, "labelFactCapacity"))
need("labelRollShape(" in cap, f"src/{LAYOUT}: labelFactCapacity() does not go by labelRollShape()")
need("LRS_SMALL" in cap and "return0" in cap, f"src/{LAYOUT}: the square is given fact lines")
need("LRS_ENDLESS" in cap and "LABEL_LAYOUT_MAX_FACTS" in cap,
     f"src/{LAYOUT}: continuous tape gets a fixed line budget")

# 2. The renderer narrows every label of the scale's own to the roll: spool,
#    filament and the test print; the square's test print has no sentence.
for fn in ("labelRenderSpool", "labelRenderFilament", "labelRenderTest"):
    need("labelLayoutForMedia(" in ks.squeeze(ks.body_of(RENDER, fn)),
         f"src/{RENDER}: {fn}() does not narrow the template to the roll")
need("labelMediaSmall(" in ks.squeeze(ks.body_of(RENDER, "labelRenderTest")),
     f"src/{RENDER}: labelRenderTest() keeps its sentence on the square")
# Continuous tape: drawn on the longest label there may be, top-aligned,
# and cut after the last line that has ink (endlessLines()).
rl = ks.squeeze(ks.body_of(RENDER, "renderLabel"))
need("endlessLines(" in rl, f"src/{RENDER}: renderLabel() does not cut a continuous label to its content")
need("BROTHER_QL_MAX_LINES" in ks.squeeze(ks.source_of(RENDER).code) or
     "labelPrinterMaxLines(" in rl, f"src/{RENDER}: no longest continuous label")
need("top" in ks.squeeze(ks.body_of(RENDER, "drawLarger")),
     f"src/{RENDER}: drawLarger() centres the lines on a continuous label")
# The raster check and the driver take any length within Brother's limits.
need("brotherQlLinesOk(" in ks.squeeze(ks.body_of(LP, "labelPrinterRasterFits")),
     f"src/{LP}: labelPrinterRasterFits() wants a fixed length on continuous tape")
need("brotherQlLinesOk(" in ks.squeeze(ks.source_of("services/brother_ql.cpp").code),
     "src/services/brother_ql.cpp: the driver wants a fixed length on continuous tape")
# The size lists name continuous tape by its width: "62 mm Endlos".
need("labelMediaName(" in ks.squeeze(ks.source_of("web/web_printer_cards.cpp").code) and
     "labelMediaName(" in ks.squeeze(ks.source_of("ui/printer_screen.cpp").code),
     "src: the size lists do not name continuous tape by its width")

# 3. The Brother keeps its model, its roll and a position per roll, under keys
#    of its own; the roll and the model are checked when they are read.
lp = ks.source_of(LP).code_str
for key in ('"printer_bqm"', '"printer_bqw"', '"printer_bqh"'):
    need(key in lp, f"src/{LP}: no key {key} for the Brother")
# The roll's own key, read through netOffsetOf() or directly.
need("brotherQlOffsetKey(" in ks.squeeze(ks.body_of(LP, "netOffsetOf")),
     f"src/{LP}: netOffsetOf() does not take the roll's own key")
net_cfg = ks.squeeze(ks.body_of(LP, "netConfig"))
need("netOffsetOf(" in net_cfg or "brotherQlOffsetKey(" in net_cfg,
     f"src/{LP}: netConfig() does not read the roll's own position")
net_save = ks.squeeze(ks.body_of(LP, "netSave"))
need("brotherQlOffsetKey(" in net_save, f"src/{LP}: netSave() does not write the roll's own position")
norm = ks.squeeze(ks.body_of(LP, "netNormalized"))
need("BROTHER_QL_LABELS[0]" not in norm or "brotherLabel(" in norm,
     f"src/{LP}: netNormalized() still fixes the Brother to its first roll")
need("LP_MODEL_BROTHER_QL700" in norm, f"src/{LP}: netNormalized() knows only one Brother")
# Where a Brother's label sits and how far it moves: the roll's own, from its table.
need("brotherQlContentX(" in ks.squeeze(ks.body_of(LP, "labelPrinterContentX")),
     f"src/{LP}: labelPrinterContentX() centres a Brother's roll instead of placing it")
need("brotherQlOffsetRange(" in ks.squeeze(ks.body_of(LP, "labelPrinterOffsetRange")),
     f"src/{LP}: labelPrinterOffsetRange() is not the roll's own for a Brother")
# The driver learns which Brother it is: the QL-700 takes other bytes.
pb = ks.squeeze(ks.body_of(LP, "printBrother"))
need("BQM_QL700" in pb or ("brotherModelOf(" in pb and
                           "BQM_QL700" in ks.squeeze(ks.body_of(LP, "brotherModelOf"))),
     f"src/{LP}: printBrother() sends every Brother the QL-810W's bytes")

# 4. The browser may pick any roll the model takes, not only the profile's
#    span (the Brother's span is 62 x 29 alone), continuous tape ("62x0") too.
media = ks.squeeze(ks.route_text("web/pages/page_printer.cpp", "/api/printer/media"))
need("labelPrinterTakesMedia(" in media,
     "src/web/pages/page_printer.cpp: /api/printer/media checks the profile's span, not the rolls")
need("l<=0" not in media and "l<0" in media,
     "src/web/pages/page_printer.cpp: /api/printer/media refuses the length 0 of continuous tape")

assert not problems, "\n" + "\n".join(f"  - {p}" for p in problems)
print("test_label_rolls: ok")
