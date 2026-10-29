"""Compile the production rules for a Klipper printer (services/klipper_rules.h) and check them: what the printer has is cut into fields (Happy Hare ahead of AFC ahead of the tool macros that carry spool_id, else one field for the active spool), the fields into units of at most four that the AMS view draws as bays, the commands that put a spool into a field, what the read-back must find, whether the location is the scale's to write, and the words for the printer's state."""
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/klipper_rules.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const char* g_what = "";
#define WHAT(s) (g_what = (s))
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL [%s] line %d: %s\n", g_what, __LINE__, #c); abort(); } } while (0)
#define CHECK_INT(a, b) do { const long long x_ = (long long)(a); const long long y_ = (long long)(b); \
  if (x_ != y_) { fprintf(stderr, "FAIL [%s] line %d: %s is %lld, expected %lld\n", g_what, __LINE__, #a, x_, y_); abort(); } } while (0)
#define CHECK_STR(a, b) do { const char* x_ = (a); const char* y_ = (b); \
  if (strcmp(x_, y_) != 0) { fprintf(stderr, "FAIL [%s] line %d: %s is \"%s\", expected \"%s\"\n", g_what, __LINE__, #a, x_, y_); abort(); } } while (0)

// Everything is built by these small helpers, with the numbers as plain ints,
// so that no brace initialisation narrows and no field is left out.
static void put(char* dst, size_t n, const char* s) { snprintf(dst, n, "%s", s); }

static KlipperRawSlot mkSlot(int spool_id, int number) {
  KlipperRawSlot s{};
  s.spool_id = spool_id;
  s.number = (int16_t)number;
  return s;
}

// Nothing found yet: no tool active, no progress known.
static KlipperRaw mkRaw() {
  KlipperRaw r{};
  r.active_tool = -1;
  r.progress = -1;
  return r;
}

// A Happy Hare with `gates` gates; spools[i] is the spool of gate i (0 none,
// the array may be null), `loaded` the gate in the extruder (-1 none). The
// count is the full number: past KL_RAW_MAX only the first slots are filled.
static KlipperRaw mkMmu(int gates, KlipperMmuMode mode, int loaded, const int* spools) {
  KlipperRaw r = mkRaw();
  r.has_mmu = true;
  r.mmu_mode = mode;
  r.mmu_count = (uint8_t)gates;
  for (int i = 0; i < gates && i < KL_RAW_MAX; i++) {
    KlipperRawSlot s = mkSlot(spools ? spools[i] : 0, i);
    s.loaded = (i == loaded);
    r.mmu[i] = s;
  }
  return r;
}

struct LaneDef { const char* lane; const char* unit; int spool; bool loaded; };

static KlipperRaw mkAfc(const LaneDef* lanes, int n) {
  KlipperRaw r = mkRaw();
  r.has_afc = true;
  r.afc_count = (uint8_t)n;
  for (int i = 0; i < n && i < KL_RAW_MAX; i++) {
    KlipperRawSlot s = mkSlot(lanes[i].spool, i);
    s.loaded = lanes[i].loaded;
    put(s.label, sizeof(s.label), lanes[i].lane);
    put(s.unit, sizeof(s.unit), lanes[i].unit);
    r.afc[i] = s;
  }
  return r;
}

// n lanes "lane1".., per_unit of them in "Turtle_1", the next ones in "Turtle_2".
static KlipperRaw mkAfcN(int n, int per_unit) {
  KlipperRaw r = mkRaw();
  r.has_afc = true;
  r.afc_count = (uint8_t)n;
  for (int i = 0; i < n && i < KL_RAW_MAX; i++) {
    KlipperRawSlot s = mkSlot(100 + i, i);
    snprintf(s.label, sizeof(s.label), "lane%d", i + 1);
    snprintf(s.unit, sizeof(s.unit), "Turtle_%d", i / per_unit + 1);
    r.afc[i] = s;
  }
  return r;
}

struct ToolDef { int number; bool has_var; int spool; };

// Tool macros as the parser lists them. `loaded` of a slot stays false: the
// active tool is the printer's one number, the rule reads it from there.
static KlipperRaw mkTools(const ToolDef* t, int n, int active, bool save_variables) {
  KlipperRaw r = mkRaw();
  r.tool_count = (uint8_t)n;
  r.active_tool = (int8_t)active;
  r.has_save_variables = save_variables;
  for (int i = 0; i < n && i < KL_RAW_MAX; i++) {
    KlipperRawSlot s = mkSlot(t[i].spool, t[i].number);
    s.has_var = t[i].has_var;
    snprintf(s.label, sizeof(s.label), "T%d", t[i].number);
    r.tool[i] = s;
  }
  return r;
}

// T0 .. T(n-1), all with the variable.
static KlipperRaw mkToolsN(int n) {
  KlipperRaw r = mkRaw();
  r.tool_count = (uint8_t)n;
  for (int i = 0; i < n && i < KL_RAW_MAX; i++) {
    KlipperRawSlot s = mkSlot(200 + i, i);
    s.has_var = true;
    snprintf(s.label, sizeof(s.label), "T%d", i);
    r.tool[i] = s;
  }
  return r;
}

// The result starts as garbage: a rule that leaves a field unwritten, or a
// text unterminated, shows.
static KlipperFacts build(const KlipperRaw& r) {
  KlipperFacts f;
  memset(&f, 0xA5, sizeof(f));
  klipperBuild(r, &f);
  return f;
}

static void expectUnit(const KlipperFacts& f, int u, int first, int count, const char* label) {
  KlipperUnit k;
  memset(&k, 0xA5, sizeof(k));
  const bool ok = klipperUnit(f, (uint8_t)u, &k);
  CHECK(ok);
  CHECK_INT(k.first, first);
  CHECK_INT(k.count, count);
  CHECK_STR(k.label, label);
}

// Whatever the layout: the units cut the fields into runs of one to four, in
// order and without a gap, a field's bay is its unit and its place in it, and
// the field of a bay is the inverse of that; a bay that is not there gives -1.
static void expectConsistent(const KlipperFacts& f) {
  const int units = klipperUnitCount(f);
  CHECK(units >= 1);
  int next = 0;
  for (int u = 0; u < units; u++) {
    KlipperUnit k;
    memset(&k, 0xA5, sizeof(k));
    const bool ok = klipperUnit(f, (uint8_t)u, &k);
    CHECK(ok);
    CHECK_INT(k.first, next);
    CHECK(k.count >= 1 && k.count <= 4);
    for (int t = 0; t < k.count; t++) {
      const int field = k.first + t;
      uint8_t a = 0xEE, b = 0xEE;
      const bool has = klipperFieldBay(f, (uint8_t)field, &a, &b);
      CHECK(has);
      CHECK_INT(a, u);
      CHECK_INT(b, t);
      CHECK_INT(klipperBayField(f, (uint8_t)u, (uint8_t)t), field);
    }
    for (int t = k.count; t < 4; t++) CHECK_INT(klipperBayField(f, (uint8_t)u, (uint8_t)t), -1);
    next += k.count;
  }
  CHECK_INT(next, f.field_count);
  KlipperUnit none;
  memset(&none, 0xA5, sizeof(none));
  CHECK(!klipperUnit(f, (uint8_t)units, &none));
  for (int t = 0; t < 4; t++) CHECK_INT(klipperBayField(f, (uint8_t)units, (uint8_t)t), -1);
  CHECK_INT(klipperBayField(f, 255, 255), -1);
  uint8_t a = 0, b = 0;
  CHECK(!klipperFieldBay(f, f.field_count, &a, &b));
  CHECK(!klipperFieldBay(f, 99, &a, &b));
  CHECK(!klipperFieldBay(f, 255, &a, &b));
}

static void expectSpools(const KlipperFacts& f, const int* want, int n) {
  CHECK_INT(f.field_count, n);
  for (int i = 0; i < n; i++) CHECK_INT(f.field[i].spool_id, want[i]);
}

// Exactly this field is the loaded one (-1: none of them).
static void expectLoaded(const KlipperFacts& f, int which) {
  for (int i = 0; i < f.field_count; i++) CHECK_INT(f.field[i].loaded, i == which ? 1 : 0);
}

static void expectPlan(const KlipperFacts& f, int field, int spool, const char* clear,
                       const char* set, bool set_active) {
  KlipperPlan p;
  memset(&p, 0xA5, sizeof(p));
  const bool ok = klipperPlanAssign(f, (uint8_t)field, spool, &p);
  CHECK(ok);
  CHECK_STR(p.clear, clear);
  CHECK_STR(p.set, set);
  CHECK_INT(p.set_active, set_active ? 1 : 0);
  CHECK_INT(p.active_spool, spool);
}

static void expectNoPlan(const KlipperFacts& f, int field, int spool) {
  KlipperPlan p;
  memset(&p, 0xA5, sizeof(p));
  CHECK(!klipperPlanAssign(f, (uint8_t)field, spool, &p));
}

int main() {
  WHAT("numbers that are stored or shared");
  CHECK_INT(KL_FIELDS_MAX, 16);
  CHECK_INT(KL_RAW_MAX, 24);
  CHECK_INT(KL_LANE_MAX, 24);
  CHECK_INT(KL_MAT_MAX, 12);
  CHECK_INT(KL_COLOR_MAX, 10);
  CHECK_INT(KL_SCRIPT_MAX, 256);
  CHECK_INT(KL_STATE_MAX, 16);
  CHECK_INT(KL_E_PARSE, -2);
  CHECK_INT(KL_E_NOT_HELD, -40);
  CHECK_INT(KL_E_OTHER_SPOOLMAN, -41);
  CHECK_INT(KL_E_NO_FIELD, -42);
  CHECK_INT(KL_E_NO_PRINTER, -43);
  // The layout is stored with a printer's last test, so its numbers are fixed.
  CHECK_INT(KL_LAYOUT_NONE, 0);
  CHECK_INT(KL_LAYOUT_SINGLE, 1);
  CHECK_INT(KL_LAYOUT_TOOLS, 2);
  CHECK_INT(KL_LAYOUT_MMU, 3);
  CHECK_INT(KL_LAYOUT_AFC, 4);
  CHECK_INT(KL_MMU_OFF, 0);
  CHECK_INT(KL_MMU_READONLY, 1);
  CHECK_INT(KL_MMU_PUSH, 2);
  CHECK_INT(KL_MMU_PULL, 3);
  CHECK_INT(KL_MMU_UNKNOWN, 4);

  WHAT("klipperMmuMode");
  CHECK_INT(klipperMmuMode("off"), KL_MMU_OFF);
  CHECK_INT(klipperMmuMode("readonly"), KL_MMU_READONLY);
  CHECK_INT(klipperMmuMode("push"), KL_MMU_PUSH);
  CHECK_INT(klipperMmuMode("pull"), KL_MMU_PULL);
  CHECK_INT(klipperMmuMode("PULL"), KL_MMU_PULL);
  CHECK_INT(klipperMmuMode("Push"), KL_MMU_PUSH);
  CHECK_INT(klipperMmuMode("READONLY"), KL_MMU_READONLY);
  CHECK_INT(klipperMmuMode("Off"), KL_MMU_OFF);
  CHECK_INT(klipperMmuMode("x"), KL_MMU_UNKNOWN);

  WHAT("Happy Hare, 8 gates, push, gate 3 loaded");
  {
    const int spools[8] = {5, 0, 7, 12, 0, 0, 9, 0};
    KlipperRaw r = mkMmu(8, KL_MMU_PUSH, 3, spools);
    put(r.mmu[0].material, sizeof(r.mmu[0].material), "PLA");
    put(r.mmu[0].color, sizeof(r.mmu[0].color), "FF0000");
    put(r.mmu[3].material, sizeof(r.mmu[3].material), "PETG");
    put(r.mmu[3].color, sizeof(r.mmu[3].color), "00FF00");
    put(r.state, sizeof(r.state), "printing");
    r.progress = 42;
    r.active_spool = 12;
    const KlipperFacts f = build(r);
    CHECK_INT(f.layout, KL_LAYOUT_MMU);
    CHECK_INT(f.mmu_mode, KL_MMU_PUSH);
    CHECK_INT(f.field_count, 8);
    CHECK(!f.truncated);
    CHECK(!f.tools_without_var);
    expectSpools(f, spools, 8);
    expectLoaded(f, 3);
    CHECK(f.field[3].loaded);
    for (int i = 0; i < 8; i++) CHECK_INT(f.field[i].number, i);
    CHECK_STR(f.field[0].material, "PLA");
    CHECK_STR(f.field[0].color, "FF0000");
    CHECK_STR(f.field[3].material, "PETG");
    CHECK_STR(f.field[3].color, "00FF00");
    CHECK_STR(f.field[1].material, "");
    CHECK_STR(f.state, "printing");
    CHECK_INT(f.progress, 42);
    CHECK_INT(f.active_spool, 12);
    CHECK_INT(klipperUnitCount(f), 2);
    expectUnit(f, 0, 0, 4, "MMU 0-3");
    expectUnit(f, 1, 4, 4, "MMU 4-7");
    expectConsistent(f);
    uint8_t a = 0xEE, b = 0xEE;
    CHECK(klipperFieldBay(f, 3, &a, &b));
    CHECK_INT(a, 0);
    CHECK_INT(b, 3);
    CHECK(klipperFieldBay(f, 4, &a, &b));
    CHECK_INT(a, 1);
    CHECK_INT(b, 0);
    CHECK(klipperFieldBay(f, 7, &a, &b));
    CHECK_INT(a, 1);
    CHECK_INT(b, 3);
    CHECK_INT(klipperBayField(f, 1, 2), 6);
  }

  WHAT("Happy Hare, 6 gates, nothing loaded");
  {
    const int spools[6] = {1, 2, 3, 4, 5, 6};
    const KlipperFacts f = build(mkMmu(6, KL_MMU_PULL, -1, spools));
    CHECK_INT(f.layout, KL_LAYOUT_MMU);
    CHECK_INT(f.mmu_mode, KL_MMU_PULL);
    expectSpools(f, spools, 6);
    expectLoaded(f, -1);
    CHECK_INT(klipperUnitCount(f), 2);
    expectUnit(f, 0, 0, 4, "MMU 0-3");
    expectUnit(f, 1, 4, 2, "MMU 4-5");
    expectConsistent(f);
  }

  WHAT("precedence: Happy Hare ahead of AFC ahead of the tools");
  {
    const int spools[4] = {1, 2, 3, 4};
    const LaneDef lanes[2] = {{"lane1", "Turtle_1", 31, false}, {"lane2", "Turtle_1", 32, true}};
    const ToolDef tools[4] = {{0, true, 21}, {1, true, 22}, {2, true, 23}, {3, true, 24}};
    KlipperRaw all = mkMmu(4, KL_MMU_READONLY, -1, spools);
    const KlipperRaw a = mkAfc(lanes, 2);
    const KlipperRaw t = mkTools(tools, 4, 1, true);
    all.has_afc = a.has_afc;
    all.afc_count = a.afc_count;
    memcpy(all.afc, a.afc, sizeof(all.afc));
    all.tool_count = t.tool_count;
    all.active_tool = t.active_tool;
    all.has_save_variables = t.has_save_variables;
    memcpy(all.tool, t.tool, sizeof(all.tool));

    KlipperFacts f = build(all);
    CHECK_INT(f.layout, KL_LAYOUT_MMU);
    expectSpools(f, spools, 4);

    all.has_mmu = false;
    f = build(all);
    CHECK_INT(f.layout, KL_LAYOUT_AFC);
    const int afc_spools[2] = {31, 32};
    expectSpools(f, afc_spools, 2);
    expectLoaded(f, 1);

    all.has_afc = false;
    f = build(all);
    CHECK_INT(f.layout, KL_LAYOUT_TOOLS);
    const int tool_spools[4] = {21, 22, 23, 24};
    expectSpools(f, tool_spools, 4);
    expectLoaded(f, 1);
  }

  WHAT("AFC, two units: Turtle_1 with four lanes, Turtle_2 with three");
  {
    const LaneDef lanes[7] = {
      {"lane1", "Turtle_1", 5, false}, {"lane2", "Turtle_1", 12, true},
      {"lane3", "Turtle_1", 0, false}, {"lane4", "Turtle_1", 0, false},
      {"lane5", "Turtle_2", 31, false}, {"lane6", "Turtle_2", 32, false},
      {"lane7", "Turtle_2", 33, false},
    };
    const int want[7] = {5, 12, 0, 0, 31, 32, 33};
    const KlipperFacts f = build(mkAfc(lanes, 7));
    CHECK_INT(f.layout, KL_LAYOUT_AFC);
    CHECK_INT(f.field_count, 7);
    CHECK(!f.truncated);
    CHECK(!f.tools_without_var);
    expectSpools(f, want, 7);
    expectLoaded(f, 1);
    for (int i = 0; i < 7; i++) {
      char lane[24];
      snprintf(lane, sizeof(lane), "lane%d", i + 1);
      CHECK_STR(f.field[i].label, lane);
      CHECK_STR(f.field[i].unit, i < 4 ? "Turtle_1" : "Turtle_2");
    }
    CHECK_INT(klipperUnitCount(f), 2);
    expectUnit(f, 0, 0, 4, "Turtle_1");
    expectUnit(f, 1, 4, 3, "Turtle_2");
    expectConsistent(f);
    uint8_t a = 0xEE, b = 0xEE;
    CHECK(klipperFieldBay(f, 4, &a, &b));
    CHECK_INT(a, 1);
    CHECK_INT(b, 0);
    CHECK(klipperFieldBay(f, 6, &a, &b));
    CHECK_INT(a, 1);
    CHECK_INT(b, 2);
    CHECK_INT(klipperBayField(f, 1, 3), -1);   // Turtle_2 has no fourth lane
    CHECK_INT(klipperBayField(f, 2, 0), -1);   // and there is no third unit
  }

  WHAT("AFC, one unit with six lanes: two units of the same name");
  {
    const LaneDef lanes[6] = {
      {"lane1", "Turtle_1", 1, false}, {"lane2", "Turtle_1", 2, false},
      {"lane3", "Turtle_1", 3, false}, {"lane4", "Turtle_1", 4, false},
      {"lane5", "Turtle_1", 5, false}, {"lane6", "Turtle_1", 6, true},
    };
    const KlipperFacts f = build(mkAfc(lanes, 6));
    CHECK_INT(f.field_count, 6);
    CHECK_INT(klipperUnitCount(f), 2);
    expectUnit(f, 0, 0, 4, "Turtle_1");
    expectUnit(f, 1, 4, 2, "Turtle_1");
    expectLoaded(f, 5);
    expectConsistent(f);
    uint8_t a = 0xEE, b = 0xEE;
    CHECK(klipperFieldBay(f, 4, &a, &b));
    CHECK_INT(a, 1);
    CHECK_INT(b, 0);
    CHECK(klipperFieldBay(f, 5, &a, &b));
    CHECK_INT(a, 1);
    CHECK_INT(b, 1);
  }

  WHAT("AFC, nine lanes in one unit, and a unit of five next to one of two");
  {
    const KlipperFacts f = build(mkAfcN(9, 9));
    CHECK_INT(klipperUnitCount(f), 3);
    expectUnit(f, 0, 0, 4, "Turtle_1");
    expectUnit(f, 1, 4, 4, "Turtle_1");
    expectUnit(f, 2, 8, 1, "Turtle_1");
    expectConsistent(f);

    const LaneDef lanes[7] = {
      {"lane1", "Turtle_1", 1, false}, {"lane2", "Turtle_1", 2, false},
      {"lane3", "Turtle_1", 3, false}, {"lane4", "Turtle_1", 4, false},
      {"lane5", "Turtle_1", 5, false},
      {"lane6", "Turtle_2", 6, false}, {"lane7", "Turtle_2", 7, false},
    };
    const KlipperFacts g = build(mkAfc(lanes, 7));
    CHECK_INT(klipperUnitCount(g), 3);
    expectUnit(g, 0, 0, 4, "Turtle_1");
    expectUnit(g, 1, 4, 1, "Turtle_1");
    expectUnit(g, 2, 5, 2, "Turtle_2");
    expectConsistent(g);
  }

  WHAT("AFC, lanes without a unit name: one unit called AFC");
  {
    const LaneDef lanes[3] = {{"lane1", "", 1, false}, {"lane2", "", 2, false}, {"lane3", "", 3, false}};
    const KlipperFacts f = build(mkAfc(lanes, 3));
    CHECK_INT(f.layout, KL_LAYOUT_AFC);
    CHECK_INT(klipperUnitCount(f), 1);
    expectUnit(f, 0, 0, 3, "AFC");
    expectConsistent(f);
  }

  WHAT("tools: T0 to T3 with the variable, T4 without");
  {
    const ToolDef tools[5] = {{0, true, 21}, {1, true, 22}, {2, true, 23}, {3, true, 24}, {4, false, 25}};
    const KlipperRaw r = mkTools(tools, 5, 1, true);
    const KlipperFacts f = build(r);
    CHECK_INT(f.layout, KL_LAYOUT_TOOLS);
    CHECK(!f.tools_without_var);
    CHECK(!f.truncated);
    CHECK(f.has_save_variables);
    const int want[4] = {21, 22, 23, 24};
    expectSpools(f, want, 4);
    for (int i = 0; i < 4; i++) CHECK_INT(f.field[i].number, i);
    expectLoaded(f, 1);
    CHECK_INT(f.active_tool, 1);
    CHECK_STR(f.state, "");
    CHECK_STR(f.unknown_system, "");
    CHECK_INT(klipperUnitCount(f), 1);
    expectUnit(f, 0, 0, 4, "T0-T3");
    expectConsistent(f);
    // The same without save_variables says so.
    CHECK(!build(mkTools(tools, 5, 1, false)).has_save_variables);
  }

  WHAT("tools: by number, not in the order they came");
  {
    const ToolDef tools[3] = {{2, true, 23}, {0, true, 21}, {1, true, 22}};
    const KlipperFacts f = build(mkTools(tools, 3, 2, false));
    CHECK_INT(f.layout, KL_LAYOUT_TOOLS);
    const int want[3] = {21, 22, 23};
    expectSpools(f, want, 3);
    for (int i = 0; i < 3; i++) CHECK_INT(f.field[i].number, i);
    expectLoaded(f, 2);
    expectUnit(f, 0, 0, 3, "T0-T2");
    expectConsistent(f);
  }

  WHAT("tools: a macro without the variable is not a field, the numbers keep their gaps");
  {
    const ToolDef tools[3] = {{0, true, 21}, {1, false, 0}, {2, true, 23}};
    const KlipperFacts f = build(mkTools(tools, 3, 2, false));
    CHECK_INT(f.layout, KL_LAYOUT_TOOLS);
    CHECK(!f.tools_without_var);
    const int want[2] = {21, 23};
    expectSpools(f, want, 2);
    CHECK_INT(f.field[0].number, 0);
    CHECK_INT(f.field[1].number, 2);
    expectLoaded(f, 1);
    expectUnit(f, 0, 0, 2, "T0-T2");
    expectConsistent(f);
  }

  WHAT("tools: five of them are two units, the second has one field");
  {
    const ToolDef tools[5] = {{0, true, 1}, {1, true, 2}, {2, true, 3}, {3, true, 4}, {4, true, 5}};
    const KlipperFacts f = build(mkTools(tools, 5, 4, false));
    CHECK_INT(f.field_count, 5);
    CHECK_INT(klipperUnitCount(f), 2);
    expectUnit(f, 0, 0, 4, "T0-T3");
    expectUnit(f, 1, 4, 1, "T4");
    expectLoaded(f, 4);
    expectConsistent(f);
  }

  WHAT("tools: a single one");
  {
    const ToolDef tools[1] = {{3, true, 24}};
    const KlipperFacts f = build(mkTools(tools, 1, 3, false));
    CHECK_INT(f.layout, KL_LAYOUT_TOOLS);
    CHECK_INT(f.field_count, 1);
    CHECK_INT(f.field[0].number, 3);
    expectLoaded(f, 0);
    expectUnit(f, 0, 0, 1, "T3");
    expectConsistent(f);
  }

  WHAT("tools: nobody active, or a number no field has");
  {
    const ToolDef tools[2] = {{0, true, 21}, {1, true, 22}};
    expectLoaded(build(mkTools(tools, 2, -1, false)), -1);
    expectLoaded(build(mkTools(tools, 2, 7, false)), -1);
  }

  WHAT("tools: macros but none with the variable: one field for the active spool");
  {
    const ToolDef tools[2] = {{0, false, 0}, {1, false, 0}};
    KlipperRaw r = mkTools(tools, 2, -1, true);
    r.active_spool = 9;
    const KlipperFacts f = build(r);
    CHECK_INT(f.layout, KL_LAYOUT_SINGLE);
    CHECK(f.tools_without_var);
    CHECK_INT(f.field_count, 1);
    CHECK_INT(f.field[0].spool_id, 9);
    CHECK(f.field[0].loaded);
    CHECK_STR(f.field[0].label, "");
    CHECK_INT(klipperUnitCount(f), 1);
    expectUnit(f, 0, 0, 1, "");
    expectConsistent(f);

    // One macro is enough to say it.
    const ToolDef one[1] = {{0, false, 0}};
    CHECK(build(mkTools(one, 1, -1, false)).tools_without_var);
    // A macro with the variable beside one without: the tools, and nothing to complain of.
    const ToolDef mixed[2] = {{0, true, 5}, {1, false, 0}};
    const KlipperFacts m = build(mkTools(mixed, 2, 0, false));
    CHECK_INT(m.layout, KL_LAYOUT_TOOLS);
    CHECK_INT(m.field_count, 1);
    CHECK(!m.tools_without_var);
  }

  WHAT("nothing at all: one field for the active spool");
  {
    KlipperRaw r = mkRaw();
    r.active_spool = 17;
    const KlipperFacts f = build(r);
    CHECK_INT(f.layout, KL_LAYOUT_SINGLE);
    CHECK_INT(f.field_count, 1);
    CHECK_INT(f.field[0].spool_id, 17);
    CHECK(f.field[0].loaded);
    CHECK_STR(f.field[0].label, "");
    CHECK_INT(f.active_spool, 17);
    CHECK(!f.truncated);
    CHECK(!f.tools_without_var);
    CHECK_STR(f.unknown_system, "");
    CHECK_INT(klipperUnitCount(f), 1);
    expectUnit(f, 0, 0, 1, "");
    expectConsistent(f);
    uint8_t a = 0xEE, b = 0xEE;
    CHECK(klipperFieldBay(f, 0, &a, &b));
    CHECK_INT(a, 0);
    CHECK_INT(b, 0);
    // No spool in the extruder is a field without a spool, still one field.
    CHECK_INT(build(mkRaw()).field[0].spool_id, 0);
    CHECK_INT(build(mkRaw()).field_count, 1);
  }

  WHAT("a system we do not know: one field, the name stays");
  {
    KlipperRaw r = mkRaw();
    r.active_spool = 3;
    put(r.unknown_system, sizeof(r.unknown_system), "box");
    const KlipperFacts f = build(r);
    CHECK_INT(f.layout, KL_LAYOUT_SINGLE);
    CHECK_INT(f.field_count, 1);
    CHECK_STR(f.unknown_system, "box");
  }

  WHAT("more fields than 16: the first 16 and a note");
  {
    KlipperFacts f = build(mkMmu(16, KL_MMU_PUSH, -1, nullptr));
    CHECK_INT(f.field_count, 16);
    CHECK(!f.truncated);
    CHECK_INT(klipperUnitCount(f), 4);
    expectConsistent(f);

    f = build(mkMmu(17, KL_MMU_PUSH, -1, nullptr));
    CHECK_INT(f.field_count, 16);
    CHECK(f.truncated);

    f = build(mkMmu(20, KL_MMU_PUSH, -1, nullptr));
    CHECK_INT(f.field_count, 16);
    CHECK(f.truncated);
    CHECK_INT(klipperUnitCount(f), 4);
    expectUnit(f, 3, 12, 4, "MMU 12-15");
    expectConsistent(f);

    // The count is the full number, also past the slots there are.
    f = build(mkMmu(30, KL_MMU_PUSH, -1, nullptr));
    CHECK_INT(f.field_count, 16);
    CHECK(f.truncated);

    f = build(mkAfcN(20, 4));
    CHECK_INT(f.layout, KL_LAYOUT_AFC);
    CHECK_INT(f.field_count, 16);
    CHECK(f.truncated);
    CHECK_INT(f.field[15].spool_id, 115);
    CHECK_INT(klipperUnitCount(f), 4);
    expectUnit(f, 3, 12, 4, "Turtle_4");
    expectConsistent(f);

    f = build(mkToolsN(20));
    CHECK_INT(f.layout, KL_LAYOUT_TOOLS);
    CHECK_INT(f.field_count, 16);
    CHECK(f.truncated);
    CHECK_INT(f.field[15].number, 15);
    CHECK_INT(f.field[15].spool_id, 215);
    expectConsistent(f);

    f = build(mkToolsN(16));
    CHECK_INT(f.field_count, 16);
    CHECK(!f.truncated);
  }

  WHAT("the command for a Happy Hare gate");
  {
    const int spools[8] = {5, 0, 7, 12, 0, 0, 9, 0};
    const KlipperMmuMode modes[4] = {KL_MMU_OFF, KL_MMU_READONLY, KL_MMU_PUSH, KL_MMU_UNKNOWN};
    for (int m = 0; m < COUNT(modes); m++) {
      const KlipperFacts f = build(mkMmu(8, modes[m], 3, spools));
      expectPlan(f, 3, 12, "", "MMU_GATE_MAP GATE=3 SPOOLID=12 QUIET=1", false);
      // Happy Hare clears the other gate itself: nothing to clear from here.
      expectPlan(f, 2, 12, "", "MMU_GATE_MAP GATE=2 SPOOLID=12 QUIET=1", false);
      expectPlan(f, 0, 77, "", "MMU_GATE_MAP GATE=0 SPOOLID=77 QUIET=1", false);
    }
    const KlipperFacts pull = build(mkMmu(8, KL_MMU_PULL, 3, spools));
    expectPlan(pull, 3, 12, "", "MMU_SPOOLMAN GATE=3 SPOOLID=12", false);
    expectPlan(pull, 6, 31, "", "MMU_SPOOLMAN GATE=6 SPOOLID=31", false);
  }

  WHAT("the command for an AFC lane");
  {
    const LaneDef lanes[4] = {
      {"lane1", "Turtle_1", 5, false}, {"lane2", "Turtle_1", 0, true},
      {"lane3", "Turtle_1", 0, false}, {"lane4", "Turtle_1", 12, false},
    };
    const KlipperFacts f = build(mkAfc(lanes, 4));
    // Spool 12 is in lane4: it is taken out there, then put into lane2. The
    // lane that is loaded makes no difference to what is sent.
    expectPlan(f, 1, 12, "SET_SPOOL_ID LANE=lane4 SPOOL_ID=", "SET_SPOOL_ID LANE=lane2 SPOOL_ID=12", false);
    // Into the lane that holds it already: nobody else to clear.
    expectPlan(f, 3, 12, "", "SET_SPOOL_ID LANE=lane4 SPOOL_ID=12", false);
    // A spool that sits nowhere.
    expectPlan(f, 0, 77, "", "SET_SPOOL_ID LANE=lane1 SPOOL_ID=77", false);

    const LaneDef thrice[4] = {
      {"lane1", "Turtle_1", 12, false}, {"lane2", "Turtle_1", 5, false},
      {"lane3", "Turtle_1", 12, false}, {"lane4", "Turtle_1", 12, false},
    };
    const KlipperFacts g = build(mkAfc(thrice, 4));
    expectPlan(g, 1, 12,
               "SET_SPOOL_ID LANE=lane1 SPOOL_ID=\nSET_SPOOL_ID LANE=lane3 SPOOL_ID=\nSET_SPOOL_ID LANE=lane4 SPOOL_ID=",
               "SET_SPOOL_ID LANE=lane2 SPOOL_ID=12", false);
  }

  WHAT("the command for a tool");
  {
    const ToolDef tools[4] = {{0, true, 0}, {1, true, 22}, {2, true, 0}, {3, true, 12}};
    // T1 is active, save_variables is there.
    const KlipperFacts f = build(mkTools(tools, 4, 1, true));
    expectPlan(f, 1, 12,
               "SET_GCODE_VARIABLE MACRO=T3 VARIABLE=spool_id VALUE=None\nSAVE_VARIABLE VARIABLE=t3__spool_id VALUE=None",
               "SET_GCODE_VARIABLE MACRO=T1 VARIABLE=spool_id VALUE=12\nSAVE_VARIABLE VARIABLE=t1__spool_id VALUE=12",
               true);
    // T3 is not the active one: the variable only.
    expectPlan(f, 3, 12, "",
               "SET_GCODE_VARIABLE MACRO=T3 VARIABLE=spool_id VALUE=12\nSAVE_VARIABLE VARIABLE=t3__spool_id VALUE=12",
               false);
    // Without save_variables nothing is saved.
    const KlipperFacts n = build(mkTools(tools, 4, 1, false));
    expectPlan(n, 1, 12,
               "SET_GCODE_VARIABLE MACRO=T3 VARIABLE=spool_id VALUE=None",
               "SET_GCODE_VARIABLE MACRO=T1 VARIABLE=spool_id VALUE=12", true);
    expectPlan(n, 3, 12, "", "SET_GCODE_VARIABLE MACRO=T3 VARIABLE=spool_id VALUE=12", false);
    // Nobody active: no tool takes the active spool along.
    expectPlan(build(mkTools(tools, 4, -1, false)), 1, 12,
               "SET_GCODE_VARIABLE MACRO=T3 VARIABLE=spool_id VALUE=None",
               "SET_GCODE_VARIABLE MACRO=T1 VARIABLE=spool_id VALUE=12", false);

    // Two others hold the spool: both are cleared, in the order of the fields.
    const ToolDef twice[4] = {{0, true, 12}, {1, true, 22}, {2, true, 12}, {3, true, 0}};
    const KlipperFacts t = build(mkTools(twice, 4, 1, true));
    expectPlan(t, 3, 12,
               "SET_GCODE_VARIABLE MACRO=T0 VARIABLE=spool_id VALUE=None\nSAVE_VARIABLE VARIABLE=t0__spool_id VALUE=None\n"
               "SET_GCODE_VARIABLE MACRO=T2 VARIABLE=spool_id VALUE=None\nSAVE_VARIABLE VARIABLE=t2__spool_id VALUE=None",
               "SET_GCODE_VARIABLE MACRO=T3 VARIABLE=spool_id VALUE=12\nSAVE_VARIABLE VARIABLE=t3__spool_id VALUE=12",
               false);
  }

  WHAT("the command for the one field");
  {
    KlipperRaw r = mkRaw();
    r.active_spool = 12;
    const KlipperFacts f = build(r);
    expectPlan(f, 0, 12, "", "", true);
    expectPlan(f, 0, 33, "", "", true);
  }

  WHAT("no command for no spool or a field that is not there");
  {
    const int spools[8] = {5, 0, 7, 12, 0, 0, 9, 0};
    const LaneDef lanes[2] = {{"lane1", "Turtle_1", 5, false}, {"lane2", "Turtle_1", 0, false}};
    const ToolDef tools[2] = {{0, true, 21}, {1, true, 22}};
    KlipperRaw one = mkRaw();
    one.active_spool = 4;
    const KlipperFacts all[4] = {
      build(mkMmu(8, KL_MMU_PUSH, 3, spools)), build(mkAfc(lanes, 2)),
      build(mkTools(tools, 2, 0, true)), build(one),
    };
    for (int i = 0; i < COUNT(all); i++) {
      expectNoPlan(all[i], 0, 0);
      expectNoPlan(all[i], 0, -5);
      expectNoPlan(all[i], all[i].field_count, 12);
      expectNoPlan(all[i], 99, 12);
      expectNoPlan(all[i], 255, 12);
    }
  }

  WHAT("what the read-back must find");
  {
    const int spools[4] = {5, 0, 7, 12};
    KlipperRaw m = mkMmu(4, KL_MMU_PUSH, 3, spools);
    m.active_spool = 12;   // the extruder's spool is not what a gate holds
    const KlipperFacts f = build(m);
    CHECK(klipperFieldHolds(f, 3, 12));
    CHECK(!klipperFieldHolds(f, 3, 13));
    CHECK(!klipperFieldHolds(f, 2, 12));
    CHECK(klipperFieldHolds(f, 2, 7));
    CHECK(!klipperFieldHolds(f, 99, 12));

    const LaneDef lanes[2] = {{"lane1", "Turtle_1", 5, false}, {"lane2", "Turtle_1", 12, true}};
    KlipperRaw a = mkAfc(lanes, 2);
    a.active_spool = 5;
    const KlipperFacts g = build(a);
    CHECK(klipperFieldHolds(g, 1, 12));
    CHECK(!klipperFieldHolds(g, 0, 12));
    CHECK(klipperFieldHolds(g, 0, 5));
    CHECK(!klipperFieldHolds(g, 2, 5));

    const ToolDef tools[2] = {{0, true, 21}, {1, true, 22}};
    KlipperRaw t = mkTools(tools, 2, 0, false);
    t.active_spool = 22;
    const KlipperFacts h = build(t);
    CHECK(klipperFieldHolds(h, 1, 22));
    CHECK(klipperFieldHolds(h, 0, 21));
    CHECK(!klipperFieldHolds(h, 0, 22));

    // The one field is read from the active spool, not from the field.
    KlipperRaw s = mkRaw();
    s.active_spool = 12;
    KlipperFacts one = build(s);
    CHECK(klipperFieldHolds(one, 0, 12));
    CHECK(!klipperFieldHolds(one, 0, 13));
    CHECK(!klipperFieldHolds(one, 1, 12));
    one.field[0].spool_id = 5;
    CHECK(klipperFieldHolds(one, 0, 12));
    CHECK(!klipperFieldHolds(one, 0, 5));
  }

  WHAT("whether the location is the scale's to write");
  {
    const KlipperMmuMode modes[5] = {KL_MMU_OFF, KL_MMU_READONLY, KL_MMU_PUSH, KL_MMU_PULL, KL_MMU_UNKNOWN};
    // Happy Hare writes the location itself in push and pull.
    const bool on[5] = {true, true, false, false, true};
    for (int m = 0; m < COUNT(modes); m++) {
      const KlipperFacts f = build(mkMmu(4, modes[m], -1, nullptr));
      CHECK_INT(klipperWritesLocation(f, true), on[m] ? 1 : 0);
      CHECK(!klipperWritesLocation(f, false));
    }
    const LaneDef lanes[1] = {{"lane1", "Turtle_1", 1, false}};
    const ToolDef tools[1] = {{0, true, 1}};
    const KlipperFacts others[3] = {build(mkAfc(lanes, 1)), build(mkTools(tools, 1, 0, false)), build(mkRaw())};
    for (int i = 0; i < COUNT(others); i++) {
      CHECK(klipperWritesLocation(others[i], true));
      CHECK(!klipperWritesLocation(others[i], false));
    }
  }

  WHAT("the printer's state in the words the AMS view knows");
  {
    CHECK_STR(klipperStateWord("printing"), "PRINTING");
    CHECK_STR(klipperStateWord("paused"), "PAUSED");
    CHECK_STR(klipperStateWord("complete"), "FINISHED");
    CHECK_STR(klipperStateWord("standby"), "IDLE");
    CHECK_STR(klipperStateWord("cancelled"), "IDLE");
    CHECK_STR(klipperStateWord("error"), "ERROR");
    // Anything else goes through as it came.
    CHECK_STR(klipperStateWord("startup"), "startup");
    CHECK_STR(klipperStateWord("shutdown"), "shutdown");
    CHECK_STR(klipperStateWord(""), "");
  }
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "klipper_rules.cpp"), "-o", str(binary)],
                   check=True)
    subprocess.run([str(binary)], check=True)

# The state words are only worth something if the AMS view knows them:
# printerActivity() in ui/ams_view.cpp matches the printer's word without case
# against its table, and a word that is not in it is printed as it came.
view = (root / "src" / "ui" / "ams_view.cpp").read_text(encoding="utf-8")
known = {w.upper() for w in re.findall(r'\{\s*"([A-Za-z]+)"\s*,\s*PRINTER_[A-Z]+\s*\}', view)}
for word in ("PRINTING", "PAUSED", "FINISHED", "IDLE", "ERROR"):
    assert word in known, f"src/ui/ams_view.cpp: printerActivity() does not know '{word}'"

print("test_klipper_rules: ok")
