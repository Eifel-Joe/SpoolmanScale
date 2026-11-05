"""The label at the scale (Spec 5.3 AC f14, V75; idea MitKIprogrammieren): the
rows a spool's label shows on the edit screen, a row changed or emptied for
one print, the free label without a spool, and the template's switches with
the same line budget the label page in the browser keeps.

Compiles the pure rules (services/label_edit_rules.h) and checks them; reads
the source for where the screen, the print sheet and the printer screen use
them."""
import subprocess
import tempfile
from pathlib import Path

import klipper_src as ks

root = Path(__file__).parents[1]
ks.assert_ci_step("test_label_edit_rules.py")

source = r"""
#include "services/label_edit_rules.h"
#include <cassert>
#include <cstring>

static const char* const CAP_COLOR = "Farbe";
static const char* const CAP_PLACE = "Lager";

static void fact(LabelFactList* f, const char* cap, const char* value) {
  const int i = f->n++;
  snprintf(f->values[i], sizeof(f->values[i]), "%s", value);
  if (cap) snprintf(f->lines[i], sizeof(f->lines[i]), "%s: %s", cap, value);
  else snprintf(f->lines[i], sizeof(f->lines[i]), "%s", value);
  f->facts[i] = LabelFact{ cap, f->values[i] };
  f->line_ptrs[i] = f->lines[i];
}

static LabelLayout layout(uint8_t preset, uint32_t fields) {
  LabelLayout l{};
  l.preset = preset;
  l.fields = fields;
  return l;
}

#define B(f) (1u << (f))
static const uint32_t HEAD3 = B(LF_VENDOR) | B(LF_MATERIAL) | B(LF_NAME);

int main() {
  SpoolLabelData d{};
  d.id = 42;
  snprintf(d.vendor, sizeof(d.vendor), "Bambu Lab");
  snprintf(d.material, sizeof(d.material), "PLA");
  snprintf(d.name, sizeof(d.name), "PLA Basic Jade White");
  LabelFactList f{};
  fact(&f, CAP_COLOR, "#F2F2E8");
  fact(&f, CAP_PLACE, "Regal 2");
  const LabelLayout std_l = layout(LABEL_PRESET_STANDARD, HEAD3 | B(LF_QR) | B(LF_COLOR) | B(LF_LOCATION));

  // 1. The rows of a spool's label: the header blocks it draws, then every
  //    fact line as the label has it; the code as it is.
  static LabelEdit e;
  labelEditFromLabel(&e, d, std_l, f, "http://bambuddy/inventory?spool=42");
  assert(!e.free && e.n == 5 && e.code && e.code_allowed);
  assert(e.row[0].part == LEP_HEAD && strcmp(e.row[0].text, "Bambu Lab") == 0);
  assert(e.row[1].part == LEP_BAND && strcmp(e.row[1].text, "PLA") == 0);
  assert(e.row[2].part == LEP_TITLE && strcmp(e.row[2].text, "PLA Basic Jade White") == 0);
  assert(e.row[3].part == LEP_FACT && strcmp(e.row[3].text, "Farbe: #F2F2E8") == 0);
  assert(e.row[4].part == LEP_FACT && strcmp(e.row[4].text, "Lager: Regal 2") == 0);
  assert(strcmp(e.qr, "http://bambuddy/inventory?spool=42") == 0);
  for (int i = 0; i < e.n; i++) assert(!e.row[i].edited);

  // Unchanged, the label comes out as it was: same header, same facts with
  // their captions, the code on.
  SpoolLabelData out = d;
  LabelFactList g{};
  LabelLayout l2 = labelEditApply(e, std_l, LABEL_MAX_FACTS, &out, &g);
  assert(l2.fields == std_l.fields && l2.preset == std_l.preset);
  assert(strcmp(out.vendor, "Bambu Lab") == 0 && strcmp(out.name, "PLA Basic Jade White") == 0);
  assert(g.n == 2 && g.facts[0].caption == CAP_COLOR && strcmp(g.facts[0].value, "#F2F2E8") == 0);
  assert(strcmp(g.line_ptrs[1], "Lager: Regal 2") == 0 && g.facts[1].caption == CAP_PLACE);

  // The compact arrangement has the maker among the facts: no head row. A
  // layout without the name has no title row.
  LabelFactList fc{};
  fact(&fc, nullptr, "Bambu Lab");
  fact(&fc, CAP_COLOR, "#F2F2E8");
  static LabelEdit ec;
  labelEditFromLabel(&ec, d, layout(LABEL_PRESET_COMPACT, HEAD3 | B(LF_QR)), fc, "x");
  assert(ec.n == 4 && ec.row[0].part == LEP_BAND && ec.row[1].part == LEP_TITLE);
  assert(strcmp(ec.row[2].text, "Bambu Lab") == 0);
  labelEditFromLabel(&ec, d, layout(LABEL_PRESET_STANDARD, B(LF_VENDOR) | B(LF_MATERIAL)), f, "");
  assert(ec.n == 4 && ec.row[1].part == LEP_BAND && ec.row[2].part == LEP_FACT);
  assert(!ec.code && !ec.code_allowed);

  // 2. A row changed for this print: its whole text, printed as one line
  //    without a caption. An emptied row is left off. The rest stays.
  assert(labelEditSet(&e, 4, "Lager: Regal 7"));
  assert(labelEditSet(&e, 3, ""));
  assert(labelEditSet(&e, 0, "Mein Hersteller"));
  assert(!labelEditSet(&e, 5, "x") && !labelEditSet(&e, -1, "x"));
  out = d;
  g = LabelFactList{};
  l2 = labelEditApply(e, std_l, LABEL_MAX_FACTS, &out, &g);
  assert(strcmp(out.vendor, "Mein Hersteller") == 0 && strcmp(out.material, "PLA") == 0);
  assert(g.n == 1 && strcmp(g.facts[0].caption, "") == 0);
  assert(strcmp(g.facts[0].value, "Lager: Regal 7") == 0 && strcmp(g.line_ptrs[0], "Lager: Regal 7") == 0);
  // The code switched off: the layout loses it, the template is not touched.
  e.code = false;
  l2 = labelEditApply(e, std_l, LABEL_MAX_FACTS, &out, &g);
  assert(!(l2.fields & B(LF_QR)) && (std_l.fields & B(LF_QR)));
  // "As the template": everything back.
  labelEditFromLabel(&e, d, std_l, f, "http://bambuddy/inventory?spool=42");
  assert(e.n == 5 && e.code && strcmp(e.row[3].text, "Farbe: #F2F2E8") == 0);

  // A long text is cut on a character boundary, never inside an umlaut.
  char longer[200];
  memset(longer, 0, sizeof(longer));
  for (int i = 0; i < 60; i++) strcat(longer, "\xC3\xBC");   // 60 x "ü", 120 bytes
  assert(labelEditSet(&e, 4, longer));
  const size_t len = strlen(e.row[4].text);
  // Typed text is cut where it prints: a header block and a fact's value
  // hold LABEL_LINE_LEN bytes (review, finding 4).
  assert(len < LABEL_LINE_LEN && len % 2 == 0);

  // 3. The free label: head, band, title and as many lines as the roll takes
  //    for the arrangement, all empty, no code until one is typed.
  static LabelEdit fr;
  const LabelLayout free_l = labelEditFreeLayout(layout(LABEL_PRESET_STANDARD, B(LF_COLOR)));
  assert(free_l.fields == (HEAD3 | B(LF_QR)) && free_l.preset == LABEL_PRESET_STANDARD);
  labelEditFree(&fr, free_l, 3);
  assert(fr.free && fr.n == 6 && !fr.code && fr.code_allowed && fr.qr[0] == '\0');
  assert(fr.row[0].part == LEP_HEAD && fr.row[3].part == LEP_FACT && fr.row[5].part == LEP_FACT);
  for (int i = 0; i < fr.n; i++) assert(fr.row[i].text[0] == '\0');
  labelEditSet(&fr, 0, "Fach 3");
  labelEditSet(&fr, 1, "PETG");
  labelEditSet(&fr, 4, "nur trocken lagern");
  SpoolLabelData blank{};
  g = LabelFactList{};
  l2 = labelEditApply(fr, free_l, LABEL_MAX_FACTS, &blank, &g);
  assert(strcmp(blank.vendor, "Fach 3") == 0 && strcmp(blank.material, "PETG") == 0 && !blank.name[0]);
  assert(g.n == 1 && strcmp(g.line_ptrs[0], "nur trocken lagern") == 0);
  assert(!(l2.fields & B(LF_QR)));           // no code: none typed
  fr.code = true;
  l2 = labelEditApply(fr, free_l, LABEL_MAX_FACTS, &blank, &g);
  assert(!(l2.fields & B(LF_QR)));           // on, but empty: still none
  assert(labelEditSetCode(&fr, "https://example.org/fach3"));
  l2 = labelEditApply(fr, free_l, LABEL_MAX_FACTS, &blank, &g);
  assert((l2.fields & B(LF_QR)) && strcmp(fr.qr, "https://example.org/fach3") == 0);
  // In the compact arrangement the head is the first line, as the maker is.
  const LabelLayout free_c = labelEditFreeLayout(layout(LABEL_PRESET_COMPACT, 0));
  labelEditFree(&fr, free_c, 4);
  assert(fr.n == 3 + 3 && fr.row[0].part == LEP_HEAD);
  labelEditSet(&fr, 0, "Fach 3");
  labelEditSet(&fr, 3, "Zeile");
  blank = SpoolLabelData{};
  g = LabelFactList{};
  labelEditApply(fr, free_c, LABEL_MAX_FACTS, &blank, &g);
  assert(!blank.vendor[0] && g.n == 2 && strcmp(g.line_ptrs[0], "Fach 3") == 0);
  // Never more lines than the roll takes now: a free label made on a
  // larger roll, printed on a smaller one, keeps its first lines only
  // (review, finding 1).
  labelEditFree(&fr, free_l, 3);
  labelEditSet(&fr, 3, "eins");
  labelEditSet(&fr, 4, "zwei");
  labelEditSet(&fr, 5, "drei");
  g = LabelFactList{};
  labelEditApply(fr, free_l, 1, &blank, &g);
  assert(g.n == 1 && strcmp(g.line_ptrs[0], "eins") == 0);
  labelEditApply(fr, free_l, 0, &blank, &g);
  assert(g.n == 0);
  // The 23 x 23 square: the band and the code, nothing else.
  const LabelLayout free_sq = labelLayoutForMedia(free_l, 23, 23);
  labelEditFree(&fr, free_sq, 0);
  assert(fr.n == 1 && fr.row[0].part == LEP_BAND && fr.code_allowed);

  // 4. The template at the scale: the line budget of the browser. A fact
  //    switch that is off stays off once the budget is full; the code and the
  //    header blocks are no fact lines; switching off is always allowed.
  const LabelLayout t3 = layout(LABEL_PRESET_STANDARD,
                                HEAD3 | B(LF_SPOOL_ID) | B(LF_COLOR) | B(LF_LOCATION));
  assert(labelLayoutFactCount(t3) == 3);
  assert(!labelLayoutMayTurnOn(t3, LF_DATE, 3));
  assert(labelLayoutMayTurnOn(t3, LF_DATE, 4));
  assert(labelLayoutMayTurnOn(t3, LF_QR, 3));
  assert(labelLayoutMayTurnOn(t3, LF_COLOR, 3));      // on already: switching it off
  // Compact: the maker counts as a fact line.
  const LabelLayout c3 = layout(LABEL_PRESET_COMPACT, B(LF_VENDOR) | B(LF_COLOR) | B(LF_LOCATION));
  assert(labelLayoutFactCount(c3) == 3 && !labelLayoutMayTurnOn(c3, LF_DATE, 3));
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


SCREEN = "ui/label_edit_screen.cpp"
code = ks.squeeze(ks.source_of(SCREEN).code)

# 1. The edit and the free label live in PSRAM, not in internal RAM: the 7B
#    has about 45 kB left in use (Punkteliste n120).
need("MALLOC_CAP_SPIRAM" in code, f"src/{SCREEN}: the label's rows are not in PSRAM")
need("staticLabelEdits_" not in code and "staticLabelEdite" not in code,
     f"src/{SCREEN}: a LabelEdit lies in internal RAM")
# 2. The keyboard is the scale's language's (Y, w02).
need("keyMapsApply(" in code, f"src/{SCREEN}: the keyboard is not the language's")
# 3. The three ways in: the print sheet's two rows, the printer screen's free label.
sheet = ks.squeeze(ks.source_of("ui/print_sheet.cpp").code)
need("labelEditOpenSpool(" in sheet and "labelEditOpenTemplate(" in sheet,
     "src/ui/print_sheet.cpp: no rows for editing the lines or the template")
need("labelEditOpenFree(" in ks.squeeze(ks.source_of("ui/printer_screen.cpp").code),
     "src/ui/printer_screen.cpp: no free label")
# 4. Not for the tape printer and not for a label FilaMan draws.
need("labelPrinterIsTape(" in code, f"src/{SCREEN}: the tape printer is not kept out")
need("labelPrinterIsTape(" in sheet and "serverLabel()" in sheet,
     "src/ui/print_sheet.cpp: the edit rows do not step aside for the tape or a FilaMan label")
# 5. The edited label is drawn by the renderer, with its rules, and printed
#    through the one door every label takes.
need("labelRenderEdit(" in code, f"src/{SCREEN}: the preview is not the renderer's")
ps = ks.squeeze(ks.source_of("ui/printer_screen.cpp").code)
need("labelRenderEdit(" in ps and "labelPrinterPrint(" in ps,
     "src/ui/printer_screen.cpp: an edited label does not go through labelPrinterPrint()")
need("labelEditApply(" in ks.squeeze(ks.body_of("services/label_render.cpp", "labelRenderEdit")),
     "src/services/label_render.cpp: labelRenderEdit() does not apply the rows")
# 6. The template's switches save the template the browser shows, with the
#    browser's budget: a filament label without a filament page has no code
#    (page_label.cpp stateJson(); review, finding 2).
need("labelQrForFilament(" in ks.squeeze(ks.body_of(SCREEN, "codePossible")),
     f"src/{SCREEN}: the template does not ask whether the filament label can have a code")
# 7. A free label belongs to the roll it was made for: another roll, new rows.
need("roll_w" in code and "roll_l" in code, f"src/{SCREEN}: a free label is not tied to its roll")
need("labelLayoutSave(" in code and "labelLayoutMayTurnOn(" in code,
     f"src/{SCREEN}: the template's switches skip the budget or the save")

assert not problems, "\n" + "\n".join(f"  - {p}" for p in problems)
print("test_label_edit_rules: ok")
