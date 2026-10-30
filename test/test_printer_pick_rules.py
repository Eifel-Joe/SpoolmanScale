"""Compile the production rules of the window "Choose printer" (beta.9, V47) and check them: the second line of a printer's button (its kind: the Bambu model, or "Klipper" with the layout and the number of tools or slots) and the rule for which buttons a word leaves in the list (name or kind holds the word, letters in any case, the capital umlauts and accents included, V43). No word shows every printer (services/printer_pick_rules.h)."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/printer_pick_rules.h"
#include <cassert>
#include <cstring>

// The limit, spelled out here on purpose: the production define must agree
// with this text, not with itself. The longest text of the table is far
// below it ("Klipper, MMU, 12 emplacements" is 29 bytes).
static_assert(PRINTER_ART_MAX == 48, "a kind of 47 bytes and a terminator");

// The German texts of the table (STR_PRL_KLIPPER, _TOOLS, _MMU, _AFC), one %d each.
static const PrinterArtTexts DE = {"Klipper", "Klipper, %d Werkzeuge", "Klipper, MMU, %d Fächer", "Klipper, AFC, %d Fächer"};
// Other words, to see that the texts come from the table and not from the rule.
static const PrinterArtTexts EN = {"Klipper", "Klipper, %d tools", "Klipper, MMU, %d slots", "Klipper, AFC, %d slots"};

static bool artIs(const PrinterPickRow& r, const PrinterArtTexts& t, const char* want) {
  // Filled with a letter first, so that a text the rule did not write (or did not
  // end) cannot pass for an empty one. The last byte stays a terminator for the compare.
  char out[PRINTER_ART_MAX];
  memset(out, 'x', sizeof(out) - 1);
  out[sizeof(out) - 1] = '\0';
  printerPickArt(r, t, out, sizeof(out));
  return strcmp(out, want) == 0;
}

// Whether the button of r stays in the list for the word: the kind is the one
// the rule makes, as the window asks for it.
static bool shown(const PrinterPickRow& r, const char* word) {
  char art[PRINTER_ART_MAX];
  printerPickArt(r, DE, art, sizeof(art));
  return printerPickShows(r, art, word);
}

int main() {
  // The printers of the first instance, as Bambuddy lists them, and a Klipper
  // printer with a filament changer.
  const PrinterPickRow antman = {"Antman",       PPK_BAMBU,   "A1 Mini", PPL_SINGLE, 0};
  const PrinterPickRow widow  = {"Black Widow",  PPK_BAMBU,   "A1",      PPL_SINGLE, 0};
  const PrinterPickRow jarvis = {"Jarvis",       PPK_BAMBU,   "X1C",     PPL_SINGLE, 0};
  const PrinterPickRow voron  = {"Voron 2.4",    PPK_KLIPPER, nullptr,   PPL_MMU,    8};

  // --- the kind of a Bambu printer: its model --------------------------------
  assert(artIs(antman, DE, "A1 Mini") && artIs(widow, DE, "A1") && artIs(jarvis, DE, "X1C"));
  // No model (FilaMan and the AMS source deliver none): nothing, the button has the name only.
  const PrinterPickRow bare  = {"Antman", PPK_BAMBU, nullptr, PPL_SINGLE, 0};
  const PrinterPickRow empty = {"Antman", PPK_BAMBU, "",      PPL_SINGLE, 0};
  assert(artIs(bare, DE, "") && artIs(empty, DE, ""));

  // --- the kind of a Klipper printer: layout and number -----------------------
  // One tool: "Klipper", the number of fields means nothing.
  const PrinterPickRow single0 = {"Voron", PPK_KLIPPER, nullptr, PPL_SINGLE, 0};
  const PrinterPickRow single4 = {"Voron", PPK_KLIPPER, nullptr, PPL_SINGLE, 4};
  assert(artIs(single0, DE, "Klipper") && artIs(single4, DE, "Klipper"));
  // A tool changer: the tools, from two on; one tool or none is no changer to speak of.
  const PrinterPickRow tools4 = {"Voron", PPK_KLIPPER, nullptr, PPL_TOOLCHANGER, 4};
  const PrinterPickRow tools1 = {"Voron", PPK_KLIPPER, nullptr, PPL_TOOLCHANGER, 1};
  const PrinterPickRow tools0 = {"Voron", PPK_KLIPPER, nullptr, PPL_TOOLCHANGER, 0};
  assert(artIs(tools4, DE, "Klipper, 4 Werkzeuge"));
  assert(artIs(tools1, DE, "Klipper") && artIs(tools0, DE, "Klipper"));
  // An MMU: the slots; none known is "Klipper".
  const PrinterPickRow mmu8 = {"Voron", PPK_KLIPPER, nullptr, PPL_MMU, 8};
  const PrinterPickRow mmu0 = {"Voron", PPK_KLIPPER, nullptr, PPL_MMU, 0};
  assert(artIs(mmu8, DE, "Klipper, MMU, 8 Fächer") && artIs(mmu0, DE, "Klipper"));
  // An AFC: the slots, too.
  const PrinterPickRow afc4 = {"Voron", PPK_KLIPPER, nullptr, PPL_AFC, 4};
  assert(artIs(afc4, DE, "Klipper, AFC, 4 Fächer"));
  // The model means nothing for Klipper: whatever stands there, the text is the same.
  const PrinterPickRow modelled_single = {"Voron", PPK_KLIPPER, "X1C",     PPL_SINGLE, 0};
  const PrinterPickRow modelled_mmu    = {"Voron", PPK_KLIPPER, "A1 Mini", PPL_MMU,    8};
  assert(artIs(modelled_single, DE, "Klipper") && artIs(modelled_mmu, DE, "Klipper, MMU, 8 Fächer"));
  // The words are the table's: another language writes the same rule with its own text.
  assert(artIs(tools4, EN, "Klipper, 4 tools") && artIs(mmu8, EN, "Klipper, MMU, 8 slots"));
  assert(artIs(afc4, EN, "Klipper, AFC, 4 slots") && artIs(single0, EN, "Klipper"));

  // --- the buffer --------------------------------------------------------------
  {
    // Too small for the text: cut, and still a string. "Klipper, MMU, 8 Fächer"
    // into 8 bytes is "Klipper" (7) and the terminator; nothing past the 8.
    char buf[16];
    memset(buf, 'x', sizeof(buf));
    printerPickArt(mmu8, DE, buf, 8);
    assert(buf[7] == '\0' && strlen(buf) == 7 && strcmp(buf, "Klipper") == 0);
    for (int i = 8; i < 16; i++) assert(buf[i] == 'x');
    // No room at all, or nowhere to write: nothing is written, nothing breaks.
    memset(buf, 'x', sizeof(buf));
    printerPickArt(mmu8, DE, buf, 0);
    for (int i = 0; i < 16; i++) assert(buf[i] == 'x');
    printerPickArt(mmu8, DE, nullptr, 8);
    printerPickArt(mmu8, DE, nullptr, 0);
  }

  // --- the word: which buttons stay --------------------------------------------
  // "jar" is Jarvis, in any case.
  assert(!shown(antman, "jar") && !shown(widow, "jar") && shown(jarvis, "jar") && !shown(voron, "jar"));
  assert(!shown(antman, "JAR") && !shown(widow, "JAR") && shown(jarvis, "JAR") && !shown(voron, "JAR"));
  // "klipper" is the Klipper printer, by its kind: its name holds no such word.
  assert(!shown(antman, "klipper") && !shown(widow, "klipper") && !shown(jarvis, "klipper") && shown(voron, "klipper"));
  // "a1" is both A1: Antman by "A1 Mini", Black Widow by "A1"; not Jarvis, not the Voron.
  assert(shown(antman, "a1") && shown(widow, "a1") && !shown(jarvis, "a1") && !shown(voron, "a1"));
  // "mini" is the A1 Mini only.
  assert(shown(antman, "mini") && !shown(widow, "mini") && !shown(jarvis, "mini") && !shown(voron, "mini"));
  // No word, an empty one or a null one: every printer.
  assert(shown(antman, "") && shown(widow, "") && shown(jarvis, "") && shown(voron, ""));
  assert(shown(antman, nullptr) && shown(widow, nullptr) && shown(jarvis, nullptr) && shown(voron, nullptr));

  // Letters are folded as the colour and type lists fold them (V43): a capital
  // umlaut typed finds the small one in the name, and the other way round.
  const PrinterPickRow buer = {"Bür", PPK_BAMBU, "", PPL_SINGLE, 0};
  assert(printerPickShows(buer, "", "BÜR") && printerPickShows(buer, "", "bür") && printerPickShows(buer, "", "BüR"));
  const PrinterPickRow big_buer = {"BÜR", PPK_BAMBU, "", PPL_SINGLE, 0};
  assert(printerPickShows(big_buer, "", "bür"));
  assert(!printerPickShows(buer, "", "bur"));    // the letter u is no u umlaut

  // No kind (null): the name alone counts.
  assert(printerPickShows(jarvis, nullptr, "jar") && printerPickShows(jarvis, nullptr, "JAR"));
  assert(!printerPickShows(jarvis, nullptr, "x1c"));
  assert(printerPickShows(jarvis, nullptr, "") && printerPickShows(jarvis, nullptr, nullptr));

  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "printer_pick_rules.cpp"),
                    str(root / "src" / "services" / "colour_search_rules.cpp"),
                    str(root / "src" / "services" / "text_fold.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("printer pick rules: ok")
