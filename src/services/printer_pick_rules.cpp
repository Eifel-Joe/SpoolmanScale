#include "services/printer_pick_rules.h"

#include <stdio.h>

#include "services/colour_search_rules.h"

void printerPickArt(const PrinterPickRow& r, const PrinterArtTexts& t, char* out, size_t n) {
  if (!out || n == 0) return;

  if (r.kind == PPK_BAMBU) {
    snprintf(out, n, "%s", r.model ? r.model : "");
    return;
  }

  // The text that counts something, when the layout has something to count.
  // A single tool is one tool whatever the test found in the fields, and one
  // tool of a changer or no number at all is no changer to speak of: those
  // stay the plain word.
  const char* fmt = nullptr;
  if (r.layout == PPL_TOOLCHANGER && r.bays >= 2)  fmt = t.tools_fmt;
  else if (r.layout == PPL_MMU && r.bays >= 1)     fmt = t.mmu_fmt;
  else if (r.layout == PPL_AFC && r.bays >= 1)     fmt = t.afc_fmt;

  // One %d in each format of the table, so one argument is all that is passed.
  if (fmt) snprintf(out, n, fmt, r.bays);
  else     snprintf(out, n, "%s", t.klipper ? t.klipper : "");
}

bool printerPickShows(const PrinterPickRow& r, const char* art, const char* word) {
  // With no word the first call already says yes; with a word a null art
  // (colourSearchMatch(): false) leaves the name alone to decide.
  return colourSearchMatch(r.name, word) || colourSearchMatch(art, word);
}
