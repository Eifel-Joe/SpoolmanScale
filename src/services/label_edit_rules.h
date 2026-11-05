#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "services/label_facts.h"
#include "services/label_layout.h"

// ============================================================
//  THE LABEL AT THE SCALE: THE RULES (Spec 5.3 AC f14, V75)
//
//  The edit screen (ui/label_edit_screen.cpp) shows a label as it
//  prints, next to its rows; a tap on a row opens the keyboard.
//  A spool's label changed for one print, a free label without a
//  spool, and the template's switches. Built after the idea of
//  MitKIprogrammieren (SpoolmanScaleatPi 8254c8ed, a branch without
//  a licence file): our own rules on our own template, renderer,
//  font rule (V45) and RGB line, nothing of its code.
//
//  Pure: no Arduino, no LVGL, no NVS. test_label_edit_rules.py.
// ============================================================

// The rows a label can have: head, band and title, then the fact lines.
#define LABEL_EDIT_ROWS (3 + LABEL_MAX_FACTS)
// A fact line as one text: the caption, ": " and the value.
#define LABEL_EDIT_TEXT (LABEL_LINE_LEN + 16)

enum LabelEditPart : uint8_t { LEP_HEAD = 0, LEP_BAND = 1, LEP_TITLE = 2, LEP_FACT = 3 };

struct LabelEditRow {
  uint8_t     part;                 // LabelEditPart
  bool        edited;               // typed: printed as one line, no caption
  const char* caption;              // a fact's caption as the label had it
  char        value[LABEL_LINE_LEN];  // a fact's value as the label had it
  char        text[LABEL_EDIT_TEXT];  // what the row shows and prints
};

// About 1.9 kB: it lives in PSRAM (the screen allocates it), never on the
// stack or in internal RAM.
struct LabelEdit {
  bool    free;          // a free label: no spool behind it
  uint8_t n;
  LabelEditRow row[LABEL_EDIT_ROWS];
  bool    code;          // the code goes on the label
  bool    code_allowed;  // the arrangement and the roll draw one at all
  char    qr[LABEL_QR_LEN];
};

// `in` into `out` (n bytes), cut on a character boundary: a cut never
// leaves half an umlaut, which the font would draw as a box.
inline void labelEditCut(const char* in, char* out, size_t n) {
  if (!out || !n) return;
  out[0] = '\0';
  if (!in) return;
  size_t len = strlen(in);
  if (len >= n) {
    len = n - 1;
    while (len > 0 && (in[len] & 0xC0) == 0x80) len--;
  }
  memcpy(out, in, len);
  out[len] = '\0';
}

// The count of fact lines a template has on, the maker among them in the
// compact arrangement: what the line budget counts (labelFactCapacity()).
inline int labelLayoutFactCount(const LabelLayout& l) {
  const uint32_t on = l.fields & labelFactFields(l.preset);
  int n = 0;
  for (int b = 0; b < 32; b++) n += (on >> b) & 1u;
  return n;
}

// Whether a template's switch may go on: always off again, and on unless it
// is a fact line and the budget is full - the rule of the label page in the
// browser (page_label.cpp capRow()).
inline bool labelLayoutMayTurnOn(const LabelLayout& l, uint8_t field, int capacity) {
  if (field >= 32 || (l.fields >> field) & 1u) return true;
  if (!((labelFactFields(l.preset) >> field) & 1u)) return true;
  return labelLayoutFactCount(l) < capacity;
}

inline LabelEditRow* labelEditAdd(LabelEdit* e, uint8_t part, const char* caption,
                                  const char* value, const char* text) {
  if (e->n >= LABEL_EDIT_ROWS) return nullptr;
  LabelEditRow& r = e->row[e->n++];
  r = LabelEditRow{};
  r.part = part;
  r.caption = caption;
  labelEditCut(value, r.value, sizeof(r.value));
  labelEditCut(text, r.text, sizeof(r.text));
  return &r;
}

// Whether the arrangement draws the maker in its header: not the compact one,
// which puts it among the facts.
inline bool labelEditHeadInHeader(const LabelLayout& l) {
  return l.preset != LABEL_PRESET_COMPACT;
}

// The rows of a spool's label as it would print: the header blocks it draws
// (`d` with `l`), then every fact line `f` has, and its code `qr`.
inline void labelEditFromLabel(LabelEdit* e, const SpoolLabelData& d, const LabelLayout& l,
                               const LabelFactList& f, const char* qr) {
  if (!e) return;
  *e = LabelEdit{};
  if (d.vendor[0] && labelLayoutHas(l, LF_VENDOR) && labelEditHeadInHeader(l))
    labelEditAdd(e, LEP_HEAD, nullptr, d.vendor, d.vendor);
  if (d.material[0] && labelLayoutHas(l, LF_MATERIAL))
    labelEditAdd(e, LEP_BAND, nullptr, d.material, d.material);
  if (d.name[0] && labelLayoutHas(l, LF_NAME))
    labelEditAdd(e, LEP_TITLE, nullptr, d.name, d.name);
  for (int i = 0; i < f.n && i < LABEL_MAX_FACTS; i++)
    labelEditAdd(e, LEP_FACT, f.facts[i].caption, f.facts[i].value, f.line_ptrs[i]);
  e->code_allowed = labelLayoutHasCode(l);
  labelEditCut(qr, e->qr, sizeof(e->qr));
  e->code = e->code_allowed && e->qr[0];
}

// The free label's template: the arrangement and the turn of `tpl`, with
// head, band, title and code; labelLayoutForMedia() then narrows it to the
// roll as every label.
inline LabelLayout labelEditFreeLayout(const LabelLayout& tpl) {
  LabelLayout l = tpl;
  l.fields = (1u << LF_VENDOR) | (1u << LF_MATERIAL) | (1u << LF_NAME) | (1u << LF_QR);
  return l;
}

// A free label: the head, band and title the layout `l` draws and `capacity`
// fact lines (in the compact arrangement the head is one of them), all
// empty, no code until one is typed.
inline void labelEditFree(LabelEdit* e, const LabelLayout& l, int capacity) {
  if (!e) return;
  *e = LabelEdit{};
  e->free = true;
  int lines = capacity;
  if (labelLayoutHas(l, LF_VENDOR)) {
    labelEditAdd(e, LEP_HEAD, nullptr, "", "");
    if (!labelEditHeadInHeader(l)) lines--;
  }
  if (labelLayoutHas(l, LF_MATERIAL)) labelEditAdd(e, LEP_BAND, nullptr, "", "");
  if (labelLayoutHas(l, LF_NAME)) labelEditAdd(e, LEP_TITLE, nullptr, "", "");
  for (int i = 0; i < lines && i < LABEL_MAX_FACTS; i++) labelEditAdd(e, LEP_FACT, nullptr, "", "");
  e->code_allowed = labelLayoutHasCode(l);
}

// Row i typed as `text`: from now on it prints as one line, without its
// caption; an empty text leaves the row off the label. False for no row i.
inline bool labelEditSet(LabelEdit* e, int i, const char* text) {
  if (!e || i < 0 || i >= e->n) return false;
  LabelEditRow& r = e->row[i];
  labelEditCut(text, r.text, sizeof(r.text));
  r.edited = true;
  return true;
}

// The code's text of a free label; on while there is one.
inline bool labelEditSetCode(LabelEdit* e, const char* text) {
  if (!e) return false;
  labelEditCut(text, e->qr, sizeof(e->qr));
  e->code = e->qr[0] != '\0';
  return true;
}

// The label `e` stands for: `d` takes the header rows (a spool's label keeps
// the rest of its data, a free one starts from nothing), `f` the fact rows
// that have a text - an untouched row with its caption and value as before,
// a typed one as one line without a caption. Returns the layout to draw it
// with: `l` without the code where it is off or empty.
inline LabelLayout labelEditApply(const LabelEdit& e, const LabelLayout& l, SpoolLabelData* d,
                                  LabelFactList* f) {
  static const char* const NO_CAPTION = "";
  LabelLayout out = l;
  if (!d || !f) return out;
  *f = LabelFactList{};
  if (e.free) *d = SpoolLabelData{};
  // In the compact arrangement a free label's head is the first fact line.
  const bool head_as_fact = e.free && !labelEditHeadInHeader(l);
  for (int i = 0; i < e.n; i++) {
    const LabelEditRow& r = e.row[i];
    const bool fact = r.part == LEP_FACT || (r.part == LEP_HEAD && head_as_fact);
    if (!fact) {
      char* field = r.part == LEP_HEAD ? d->vendor : r.part == LEP_BAND ? d->material : d->name;
      labelEditCut(r.text, field, LABEL_LINE_LEN);
      continue;
    }
    if (!r.text[0] || f->n >= LABEL_MAX_FACTS) continue;
    const int k = f->n++;
    const bool as_was = !r.edited && !e.free;
    labelEditCut(as_was ? r.value : r.text, f->values[k], sizeof(f->values[k]));
    labelEditCut(r.text, f->lines[k], sizeof(f->lines[k]));
    f->facts[k] = LabelFact{ as_was ? r.caption : NO_CAPTION, f->values[k] };
    f->line_ptrs[k] = f->lines[k];
  }
  if (!e.code || !e.qr[0]) out.fields &= ~(1u << LF_QR);
  return out;
}
