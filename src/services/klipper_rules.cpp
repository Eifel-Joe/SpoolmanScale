#include "services/klipper_rules.h"

namespace {

const int FIELDS_PER_UNIT = 4;   // bays of one AMS unit (AMS_MAX_TRAYS)
const int UNITS_MAX = 4;         // units of one printer (AMS_MAX_UNITS)

// ------------------------------------------------------------
//  Small text helpers. No snprintf: a built text that does not fit is
//  reported (ok == false) instead of cut, and nothing here can raise a
//  truncation warning.
// ------------------------------------------------------------

struct Text {
  char* dst;
  size_t cap;
  size_t len;
  bool ok;     // false once a part did not fit
};

void textInit(Text* t, char* dst, size_t cap) {
  t->dst = dst;
  t->cap = cap;
  t->len = 0;
  t->ok = cap > 0;
  if (cap > 0) dst[0] = '\0';
}

void textAdd(Text* t, const char* s) {
  if (!t->ok) return;
  const size_t n = strlen(s);
  if (t->len + n + 1 > t->cap) {
    t->ok = false;
    return;
  }
  memcpy(t->dst + t->len, s, n + 1);
  t->len += n;
}

void textInt(Text* t, int v) {
  char rev[16];
  int n = 0;
  unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
  do {
    rev[n++] = (char)('0' + u % 10u);
    u /= 10u;
  } while (u != 0 && n < 12);
  if (v < 0) rev[n++] = '-';
  char out[16];
  for (int i = 0; i < n; i++) out[i] = rev[n - 1 - i];
  out[n] = '\0';
  textAdd(t, out);
}

// The next command goes on a line of its own.
void textLine(Text* t) {
  if (t->len > 0) textAdd(t, "\n");
}

// Copies at most cap - 1 characters and always terminates. A null is "".
void copyStr(char* dst, size_t cap, const char* src) {
  if (cap == 0) return;
  size_t i = 0;
  if (src) {
    for (; src[i] != '\0' && i + 1 < cap; i++) dst[i] = src[i];
  }
  dst[i] = '\0';
}

char lowerAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

bool sameNoCase(const char* a, const char* b) {
  while (*a != '\0' && *b != '\0') {
    if (lowerAscii(*a) != lowerAscii(*b)) return false;
    a++;
    b++;
  }
  return *a == *b;
}

int fieldTotal(const KlipperFacts& f) {
  return f.field_count < KL_FIELDS_MAX ? f.field_count : KL_FIELDS_MAX;
}

// ------------------------------------------------------------
//  Building the fields
// ------------------------------------------------------------

void fieldFromSlot(KlipperField* field, const KlipperRawSlot& s, bool loaded) {
  field->spool_id = s.spool_id > 0 ? s.spool_id : 0;
  field->number = s.number;
  field->loaded = loaded;
  copyStr(field->label, sizeof(field->label), s.label);
  copyStr(field->unit, sizeof(field->unit), s.unit);
  copyStr(field->material, sizeof(field->material), s.material);
  copyStr(field->color, sizeof(field->color), s.color);
}

// The tool macros that have the variable, as indexes into raw.tool ordered by
// their number (equal numbers keep the order they came in). Only the slots
// the parser filled are looked at.
int toolOrder(const KlipperRaw& raw, uint8_t* order) {
  const int seen = raw.tool_count < KL_RAW_MAX ? raw.tool_count : KL_RAW_MAX;
  int n = 0;
  for (int i = 0; i < seen; i++) {
    if (!raw.tool[i].has_var) continue;
    int k = n++;
    while (k > 0 && raw.tool[order[k - 1]].number > raw.tool[i].number) {
      order[k] = order[k - 1];
      k--;
    }
    order[k] = (uint8_t)i;
  }
  return n;
}

// How many of the `count` slots become fields: all of them up to the limit,
// and never more than the parser kept.
int fieldsOf(int count) {
  int n = count < KL_FIELDS_MAX ? count : KL_FIELDS_MAX;
  return n < KL_RAW_MAX ? n : KL_RAW_MAX;
}

// ------------------------------------------------------------
//  Cutting the fields into units
// ------------------------------------------------------------

struct Run {
  int first;
  int count;
};

// Runs of four in field order. For 16 fields at most, so never past UNITS_MAX;
// the loop holds that for any count.
int groupsOfFour(int total, Run* runs) {
  int n = 0;
  for (int first = 0; first < total && n < UNITS_MAX; first += FIELDS_PER_UNIT) {
    runs[n].first = first;
    runs[n].count = total - first < FIELDS_PER_UNIT ? total - first : FIELDS_PER_UNIT;
    n++;
  }
  return n;
}

// AFC: the fields that follow each other with the same unit name are one
// unit, split in fours. A name that comes again later is another unit: a unit
// is a run of fields without a gap, the view addresses fields by their place
// in it. -1 when this makes more than UNITS_MAX.
int groupsByName(const KlipperFacts& f, int total, Run* runs) {
  int n = 0;
  int i = 0;
  while (i < total) {
    int end = i + 1;
    while (end < total && strcmp(f.field[end].unit, f.field[i].unit) == 0) end++;
    for (int first = i; first < end; first += FIELDS_PER_UNIT) {
      if (n == UNITS_MAX) return -1;
      runs[n].first = first;
      runs[n].count = end - first < FIELDS_PER_UNIT ? end - first : FIELDS_PER_UNIT;
      n++;
    }
    i = end;
  }
  return n;
}

// Root: the AMS view has room for four units besides its external slot
//   (AMS_MAX_UNITS), and AFC units are named by the printer's config, so
//   six units of two lanes (or three of five, which split into 4+1 each) are
//   more than the view can draw (R7, R10).
// Fix: where the names would make more than four units, they are dropped:
//   all fields go into groups of four in field order, labelled "AFC". The
//   count is read on the fields after the cut to 16, which is what is drawn.
// NOT-TO-DO: merging neighbouring units until four are left. The merged
//   unit would carry a name that is true for half of its lanes. Nor drawing
//   a fifth unit: AMS_UNITS_TOTAL is 5 because the external slot takes the
//   last place.
// See test_klipper_rules.py "AFC, six units of two lanes".
// `named` tells whether the labels are the unit names.
int unitRuns(const KlipperFacts& f, Run* runs, bool* named) {
  const int total = fieldTotal(f);
  *named = false;
  if (f.layout == KL_LAYOUT_AFC) {
    const int n = groupsByName(f, total, runs);
    if (n >= 0) {
      *named = true;
      return n;
    }
  }
  return groupsOfFour(total, runs);
}

void unitLabel(const KlipperFacts& f, const Run& r, bool named, char* dst, size_t cap) {
  Text t;
  textInit(&t, dst, cap);
  const KlipperField& a = f.field[r.first];
  const KlipperField& z = f.field[r.first + r.count - 1];
  switch (f.layout) {
    case KL_LAYOUT_MMU:
      textAdd(&t, "MMU ");
      textInt(&t, a.number);
      if (r.count > 1) {
        textAdd(&t, "-");
        textInt(&t, z.number);
      }
      break;
    case KL_LAYOUT_TOOLS:
      textAdd(&t, "T");
      textInt(&t, a.number);
      if (r.count > 1) {
        textAdd(&t, "-T");
        textInt(&t, z.number);
      }
      break;
    case KL_LAYOUT_AFC:
      textAdd(&t, (named && a.unit[0] != '\0') ? a.unit : "AFC");
      break;
    default:
      break;   // the one field: the view puts its own caption
  }
  if (!t.ok && cap > 0) dst[0] = '\0';
}

// ------------------------------------------------------------
//  The commands
// ------------------------------------------------------------

// One lane: SET_SPOOL_ID with the spool, or without a value to empty it
// (spool_id 0). False for a lane that has no name to address it by.
bool laneLine(Text* t, const KlipperField& lane, int spool_id) {
  if (lane.label[0] == '\0') return false;
  textAdd(t, "SET_SPOOL_ID LANE=");
  textAdd(t, lane.label);
  textAdd(t, " SPOOL_ID=");
  if (spool_id > 0) textInt(t, spool_id);
  return true;
}

// Root: the commands the printer is allowed to receive are fixed (global
//   constraints): MACRO=T<n> and the saved variable t<n>__spool_id.
// Fix: both names are written from the field's number, whatever the label
//   says. The parser takes only "T<n>" (upper case, no leading zero, R37) as
//   a tool macro, so the label and the written name agree.
// NOT-TO-DO: using the field's label: should the parser ever take another
//   spelling, a macro spelled t1 would be addressed as t1, a command nobody
//   agreed to send.
// `spool_id` 0 writes None.
void toolLines(Text* t, const KlipperFacts& f, int number, int spool_id) {
  textAdd(t, "SET_GCODE_VARIABLE MACRO=T");
  textInt(t, number);
  textAdd(t, " VARIABLE=spool_id VALUE=");
  if (spool_id > 0) textInt(t, spool_id); else textAdd(t, "None");
  if (f.has_save_variables) {
    textAdd(t, "\nSAVE_VARIABLE VARIABLE=t");
    textInt(t, number);
    textAdd(t, "__spool_id VALUE=");
    if (spool_id > 0) textInt(t, spool_id); else textAdd(t, "None");
  }
}

void planMmu(const KlipperFacts& f, const KlipperField& target, int spool_id, Text* set) {
  if (f.mmu_mode == KL_MMU_PULL) {
    // The gate is taken from Spoolman: the printer reads the spool itself.
    textAdd(set, "MMU_SPOOLMAN GATE=");
    textInt(set, target.number);
    textAdd(set, " SPOOLID=");
    textInt(set, spool_id);
  } else {
    textAdd(set, "MMU_GATE_MAP GATE=");
    textInt(set, target.number);
    textAdd(set, " SPOOLID=");
    textInt(set, spool_id);
    textAdd(set, " QUIET=1");
  }
}

// Root: a spool sits in one place. If another lane or tool still holds the
//   spool when it is put into this one, the printer has it twice.
// Fix: the clear script empties every other field with the same spool first,
//   in the order of the fields; the glue sends it before the set script.
//   Happy Hare does this itself, so its clear script stays empty.
// NOT-TO-DO: emptying the target as well. The set line overwrites it, and a
//   lane that holds the spool already needs no clearing at all.
// See test_klipper_rules.py "the command for an AFC lane" and "for a tool".
bool planOthers(const KlipperFacts& f, uint8_t field, int spool_id, Text* clear) {
  const int total = fieldTotal(f);
  for (int i = 0; i < total; i++) {
    if (i == field || f.field[i].spool_id != spool_id) continue;
    textLine(clear);
    if (f.layout == KL_LAYOUT_AFC) {
      if (!laneLine(clear, f.field[i], 0)) return false;
    } else {
      toolLines(clear, f, f.field[i].number, 0);
    }
  }
  return true;
}

}  // namespace

KlipperMmuMode klipperMmuMode(const char* spoolman_support) {
  if (!spoolman_support) return KL_MMU_UNKNOWN;
  if (sameNoCase(spoolman_support, "off")) return KL_MMU_OFF;
  if (sameNoCase(spoolman_support, "readonly")) return KL_MMU_READONLY;
  if (sameNoCase(spoolman_support, "push")) return KL_MMU_PUSH;
  if (sameNoCase(spoolman_support, "pull")) return KL_MMU_PULL;
  return KL_MMU_UNKNOWN;
}

// Root: a printer can report several systems at once (a Happy Hare and tool
//   macros that stay from an older setup), and the fields must come from one.
// Fix: the first that has slots wins: Happy Hare, AFC, the tool macros that
//   carry the variable, else the one active spool. A Happy Hare or AFC that
//   reports no slot at all draws nothing, so it is passed over.
// NOT-TO-DO: adding the systems up. A gate and the tool macro that stands
//   for it would be two bays for one spool. Nor taking tools without the
//   variable as fields: their spool_id cannot be set (tools_without_var says
//   so, and the view explains why the printer shows one field).
// See test_klipper_rules.py "precedence".
void klipperBuild(const KlipperRaw& raw, KlipperFacts* out) {
  memset(out, 0, sizeof(*out));
  out->has_save_variables = raw.has_save_variables;
  out->active_spool = raw.active_spool;
  out->active_tool = raw.active_tool;
  out->progress = raw.progress;
  copyStr(out->state, sizeof(out->state), raw.state);
  copyStr(out->unknown_system, sizeof(out->unknown_system), raw.unknown_system);

  int total = 0;     // fields the printer has, before the cut
  int shown = 0;
  bool unseen = false;   // the parser kept fewer slots than there are
  if (raw.has_mmu && raw.mmu_count > 0) {
    out->layout = KL_LAYOUT_MMU;
    out->mmu_mode = raw.mmu_mode;
    total = raw.mmu_count;
    shown = fieldsOf(total);
    for (int i = 0; i < shown; i++) fieldFromSlot(&out->field[i], raw.mmu[i], raw.mmu[i].loaded);
  } else if (raw.has_afc && raw.afc_count > 0) {
    out->layout = KL_LAYOUT_AFC;
    total = raw.afc_count;
    shown = fieldsOf(total);
    for (int i = 0; i < shown; i++) fieldFromSlot(&out->field[i], raw.afc[i], raw.afc[i].loaded);
  } else {
    uint8_t order[KL_RAW_MAX] = {};
    const int vars = toolOrder(raw, order);
    if (vars > 0) {
      out->layout = KL_LAYOUT_TOOLS;
      // The parser keeps KL_RAW_MAX macros: with more than that, some were
      // not seen and the list may be short.
      unseen = raw.tool_count > KL_RAW_MAX;
      total = vars;
      shown = vars < KL_FIELDS_MAX ? vars : KL_FIELDS_MAX;
      for (int i = 0; i < shown; i++) {
        const KlipperRawSlot& s = raw.tool[order[i]];
        fieldFromSlot(&out->field[i], s, raw.active_tool >= 0 && s.number == raw.active_tool);
      }
    } else {
      out->layout = KL_LAYOUT_SINGLE;
      out->tools_without_var = raw.tool_count > 0;
      total = 1;
      shown = 1;
      out->field[0].spool_id = raw.active_spool > 0 ? raw.active_spool : 0;
      out->field[0].loaded = true;
    }
  }
  out->field_count = (uint8_t)shown;
  out->truncated = total > KL_FIELDS_MAX || unseen;
}

uint8_t klipperUnitCount(const KlipperFacts& f) {
  Run runs[UNITS_MAX] = {};
  bool named = false;
  return (uint8_t)unitRuns(f, runs, &named);
}

bool klipperUnit(const KlipperFacts& f, uint8_t unit, KlipperUnit* out) {
  memset(out, 0, sizeof(*out));
  Run runs[UNITS_MAX] = {};
  bool named = false;
  const int n = unitRuns(f, runs, &named);
  if (unit >= n) return false;
  out->first = (uint8_t)runs[unit].first;
  out->count = (uint8_t)runs[unit].count;
  unitLabel(f, runs[unit], named, out->label, sizeof(out->label));
  return true;
}

bool klipperFieldBay(const KlipperFacts& f, uint8_t field, uint8_t* ams_id, uint8_t* tray_id) {
  if (!ams_id || !tray_id) return false;
  Run runs[UNITS_MAX] = {};
  bool named = false;
  const int n = unitRuns(f, runs, &named);
  for (int u = 0; u < n; u++) {
    if (field >= runs[u].first && field < runs[u].first + runs[u].count) {
      *ams_id = (uint8_t)u;
      *tray_id = (uint8_t)(field - runs[u].first);
      return true;
    }
  }
  return false;
}

int klipperBayField(const KlipperFacts& f, uint8_t ams_id, uint8_t tray_id) {
  Run runs[UNITS_MAX] = {};
  bool named = false;
  const int n = unitRuns(f, runs, &named);
  if (ams_id >= n || tray_id >= runs[ams_id].count) return -1;
  return runs[ams_id].first + tray_id;
}

bool klipperPlanAssign(const KlipperFacts& f, uint8_t field, int spool_id, KlipperPlan* out) {
  memset(out, 0, sizeof(*out));
  if (spool_id <= 0 || field >= fieldTotal(f)) return false;
  const KlipperField& target = f.field[field];
  Text clear;
  Text set;
  textInit(&clear, out->clear, sizeof(out->clear));
  textInit(&set, out->set, sizeof(out->set));

  bool ok = true;
  switch (f.layout) {
    case KL_LAYOUT_MMU:
      planMmu(f, target, spool_id, &set);
      break;
    case KL_LAYOUT_AFC:
      ok = planOthers(f, field, spool_id, &clear) && laneLine(&set, target, spool_id);
      break;
    case KL_LAYOUT_TOOLS:
      ok = planOthers(f, field, spool_id, &clear);
      toolLines(&set, f, target.number, spool_id);
      // The active tool's spool is the printer's active spool as well.
      out->set_active = f.active_tool >= 0 && target.number == f.active_tool;
      break;
    case KL_LAYOUT_SINGLE:
      // No G-code: the active spool is the field.
      out->set_active = true;
      break;
    default:
      ok = false;
      break;
  }
  // A script that did not fit is not sent half: R8.
  if (!ok || !clear.ok || !set.ok) {
    memset(out, 0, sizeof(*out));
    return false;
  }
  out->active_spool = spool_id;
  return true;
}

bool klipperFieldHolds(const KlipperFacts& after, uint8_t field, int spool_id) {
  if (field >= fieldTotal(after)) return false;
  if (after.layout == KL_LAYOUT_SINGLE) return after.active_spool == spool_id;
  return after.field[field].spool_id == spool_id;
}

bool klipperWritesLocation(const KlipperFacts& f, bool switch_on) {
  if (!switch_on) return false;
  const bool hh_writes = f.layout == KL_LAYOUT_MMU &&
                         (f.mmu_mode == KL_MMU_PUSH || f.mmu_mode == KL_MMU_PULL);
  return !hh_writes;
}

const char* klipperStateWord(const char* print_stats_state) {
  struct Word {
    const char* from;
    const char* to;
  };
  static const Word WORDS[] = {
    {"printing", "PRINTING"}, {"paused", "PAUSED"},   {"complete", "FINISHED"},
    {"standby", "IDLE"},      {"cancelled", "IDLE"},  {"error", "ERROR"},
  };
  if (!print_stats_state) return "";
  for (const Word& w : WORDS) {
    if (strcmp(print_stats_state, w.from) == 0) return w.to;
  }
  return print_stats_state;
}
