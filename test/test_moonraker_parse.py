"""Compile the production reading of Moonraker's answers (services/moonraker_parse.h, with ArduinoJson from argv[1]) and run it over the answers in test/fixtures/moonraker/<case>/: the list of objects to the names of what the printer has, the names to the query that asks only for that, the answer of the query to the raw facts, the facts to the fields the AMS view draws; then the answers about the server (state, Spoolman, the address of Spoolman), the active spool and the newest spool of a Spoolman.

Where the fixtures come from. Every file is a whole HTTP body as Moonraker sends it, {"result": ...}; the test hands the parser doc["result"], and doc["result"]["status"] for the query.
  hulk_single     Real answers of Joerg's Voron "Hulk" (Moonraker v0.11.0-4-gb559b9a), read on 29.10.2026 and anonymised (the address is 192.0.2.178, there is no key): one extruder, save_variables, the macros TURN_OFF_HEATERS, TEST_FLOW and TEST_PA, which are no tools, the active spool 14. The query in query.json was asked of every system, and so it carries mmu, AFC and toolchanger with null fields although the printer has none of them; query_macro_probe.json is the answer to a query that asked two macros for the field spool_id only.
  The others are built from the shapes the research documents (docs/recherche/2026-10-29-beta8/moonraker.md, in the meta repository), nothing is invented beyond that, until real answers replace them:
  hh_push_8       Happy Hare, 8 gates, gate 3 loaded, spoolman_support push: 3.4 (the fields of mmu), 4.1 (the modes), 8a and 8b (server/info, spoolman/status, server/config).
  hh_pull_4       Happy Hare, 4 gates, pull, nothing loaded (gate -1), Spoolman not connected, the address of Spoolman without a scheme (8b: it may be missing).
  afc_2units      AFC, Turtle_1 with four lanes and Turtle_2 with three, lane2 loaded: 3.5 (AFC, AFC_BoxTurtle, AFC_stepper and its fields spool_id, material, color, unit, tool_loaded).
  toolchanger_4   klipper-toolchanger, T0 to T3 with the variable spool_id, save_variables, tool 1 active: 3.3 and 4.3 (the macros of Fluidd and Mainsail, toolchanger.tool_number).
  tools_no_var    two macros T0 and T1 without spool_id (a macro with only variable_color, as the StealthChanger has it, and one with none): 3.3, 4.3.
  single          one extruder and nothing else: 3.2.
  cfs_box         a Creality with the closed object box and no [spoolman] in Moonraker: 7.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
fixtures = root / "test" / "fixtures" / "moonraker"

if len(sys.argv) < 2:
    sys.exit("usage: python3 test/test_moonraker_parse.py <folder of ArduinoJson.h>   "
             "(CI: .pio/libdeps/wt32-sc01-plus/ArduinoJson/src)")
arduinojson = Path(sys.argv[1]).resolve()
assert (arduinojson / "ArduinoJson.h").is_file(), f"{arduinojson}: no ArduinoJson.h in there"

source = r'''
#include "services/moonraker_parse.h"
#include "services/klipper_rules.h"
#include "services/klipper_test_rules.h"
#include <ArduinoJson.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

static const char* g_what = "";
#define WHAT(s) (g_what = (s))
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL [%s] line %d: %s\n", g_what, __LINE__, #c); abort(); } } while (0)
#define CHECK_INT(a, b) do { const long long x_ = (long long)(a); const long long y_ = (long long)(b); \
  if (x_ != y_) { fprintf(stderr, "FAIL [%s] line %d: %s is %lld, expected %lld\n", g_what, __LINE__, #a, x_, y_); abort(); } } while (0)
#define CHECK_STR(a, b) do { const char* x_ = (a); const char* y_ = (b); \
  if (strcmp(x_, y_) != 0) { fprintf(stderr, "FAIL [%s] line %d: %s is \"%s\", expected \"%s\"\n", g_what, __LINE__, #a, x_, y_); abort(); } } while (0)

static std::string g_dir;          // test/fixtures/moonraker
static std::string g_case;         // the case whose files are read

// What the parser fills. Zeroed before every use: it is the caller's struct.
static KlipperObjectNames g_names;
static KlipperRaw g_raw;
static KlipperFacts g_facts;

// ---- files and JSON ---------------------------------------------------------

static std::string slurp(const std::string& path) {
  std::ifstream f(path.c_str(), std::ios::binary);
  if (!f.good()) {
    fprintf(stderr, "FAIL [%s]: cannot read %s\n", g_what, path.c_str());
    abort();
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static bool haveFile(const char* file) {
  std::ifstream f((g_dir + "/" + g_case + "/" + file).c_str());
  return f.good();
}

static void parseText(JsonDocument& doc, const char* text) {
  doc.clear();
  const DeserializationError err = deserializeJson(doc, text);
  if (err) {
    fprintf(stderr, "FAIL [%s]: JSON: %s in: %s\n", g_what, err.c_str(), text);
    abort();
  }
}

static void loadFrom(JsonDocument& doc, const char* dir, const char* file) {
  parseText(doc, slurp(g_dir + "/" + dir + "/" + file).c_str());
}

static void load(JsonDocument& doc, const char* file) { loadFrom(doc, g_case.c_str(), file); }

static JsonVariantConst whole(const JsonDocument& d) { return d.as<JsonVariantConst>(); }
static JsonVariantConst result(const JsonDocument& d) { return d.as<JsonVariantConst>()["result"]; }
static JsonVariantConst status(const JsonDocument& d) { return d.as<JsonVariantConst>()["result"]["status"]; }

static const char* str(JsonVariantConst v) {
  const char* s = v.as<const char*>();
  return s ? s : "";
}

static bool listHas(JsonVariantConst arr, const char* s) {
  for (JsonVariantConst v : arr.as<JsonArrayConst>()) {
    if (strcmp(str(v), s) == 0) return true;
  }
  return false;
}

// ---- the query body ---------------------------------------------------------

static bool queryHasKey(const JsonDocument& body, const char* object) {
  JsonObjectConst objs = body.as<JsonVariantConst>()["objects"].as<JsonObjectConst>();
  for (JsonPairConst kv : objs) {
    if (strcmp(kv.key().c_str(), object) == 0) return true;
  }
  return false;
}

// The object is asked for, with at least these fields (space separated).
static void expectAsked(const JsonDocument& body, const char* object, const char* fields) {
  CHECK(queryHasKey(body, object));
  JsonVariantConst list = body.as<JsonVariantConst>()["objects"][object];
  CHECK(list.is<JsonArrayConst>());
  std::string rest = fields;
  size_t at = 0;
  while (at < rest.size()) {
    size_t end = rest.find(' ', at);
    if (end == std::string::npos) end = rest.size();
    const std::string one = rest.substr(at, end - at);
    if (!listHas(list, one.c_str())) {
      fprintf(stderr, "FAIL [%s]: \"%s\" is not asked for the field \"%s\"\n", g_what, object, one.c_str());
      abort();
    }
    at = end + 1;
  }
}

// The object is asked for with no list of fields: null, so that the answer
// holds every variable it really has.
static void expectAskedFull(const JsonDocument& body, const char* object) {
  CHECK(queryHasKey(body, object));
  CHECK(body.as<JsonVariantConst>()["objects"][object].isNull());
}

static void expectNotAsked(const JsonDocument& body, const char* object) {
  if (queryHasKey(body, object)) {
    fprintf(stderr, "FAIL [%s]: \"%s\" is asked although the printer does not list it\n", g_what, object);
    abort();
  }
}

static void expectBodyIsObjects(const JsonDocument& body) {
  CHECK(body.as<JsonVariantConst>()["objects"].is<JsonObjectConst>());
}

// ---- the pipeline of one case -----------------------------------------------

// objects_list -> names -> query body -> query answer -> raw -> facts, the way
// moonrakerFetchFacts runs it; the active spool comes from its own file.
static void runCase(const char* name, JsonDocument& body) {
  g_case = name;
  WHAT(name);
  JsonDocument doc;

  load(doc, "objects_list.json");
  memset(&g_names, 0, sizeof(g_names));
  CHECK(moonrakerParseObjectList(result(doc), &g_names));

  body.clear();
  moonrakerBuildQuery(g_names, body);
  expectBodyIsObjects(body);

  load(doc, "query.json");
  memset(&g_raw, 0, sizeof(g_raw));
  CHECK(moonrakerParseQuery(status(doc), g_names, &g_raw));

  if (haveFile("spool_id.json")) {
    load(doc, "spool_id.json");
    g_raw.active_spool = moonrakerParseActiveSpool(result(doc));
  }
  memset(&g_facts, 0xA5, sizeof(g_facts));
  klipperBuild(g_raw, &g_facts);
}

static void expectNames(bool mmu, bool afc, bool toolchanger, bool save_variables, int macros, int lanes) {
  CHECK_INT(g_names.has_mmu, mmu ? 1 : 0);
  CHECK_INT(g_names.has_afc, afc ? 1 : 0);
  CHECK_INT(g_names.has_toolchanger, toolchanger ? 1 : 0);
  CHECK_INT(g_names.has_save_variables, save_variables ? 1 : 0);
  CHECK(g_names.has_print_stats);
  CHECK(g_names.has_virtual_sdcard);
  CHECK_INT(g_names.macro_count, macros);
  CHECK_INT(g_names.lane_count, lanes);
}

static void expectUnitLabel(const KlipperFacts& f, int u, int first, int count, const char* label) {
  KlipperUnit k;
  memset(&k, 0xA5, sizeof(k));
  const bool ok = klipperUnit(f, (uint8_t)u, &k);
  CHECK(ok);
  CHECK_INT(k.first, first);
  CHECK_INT(k.count, count);
  CHECK_STR(k.label, label);
}

// Exactly this field is loaded (-1: none).
static void expectLoadedField(const KlipperFacts& f, int which) {
  for (int i = 0; i < f.field_count; i++) CHECK_INT(f.field[i].loaded, i == which ? 1 : 0);
}

static void expectStatePercent(const char* state, int percent) {
  CHECK_STR(g_raw.state, state);
  CHECK_INT(g_raw.progress, percent);
  CHECK_STR(g_facts.state, state);
  CHECK_INT(g_facts.progress, percent);
}

// The answers about the server of the case: the state and the component
// spoolman, the status of the connection to Spoolman where the case has that
// file (has_status), and the address of Spoolman where `server` is given.
static void expectServer(bool spoolman, bool has_status, bool connected, const char* server) {
  JsonDocument doc;
  load(doc, "server_info.json");
  char state[KL_STATE_MAX];
  memset(state, 0xA5, sizeof(state));
  bool has = !spoolman;                      // the opposite: the parser has to write it
  CHECK(moonrakerParseServerInfo(result(doc), state, sizeof(state), &has));
  CHECK_STR(state, "ready");
  CHECK_INT(has, spoolman ? 1 : 0);

  if (has_status) {
    load(doc, "spoolman_status.json");
    bool up = !connected;
    CHECK(moonrakerParseSpoolmanStatus(result(doc), &up));
    CHECK_INT(up, connected ? 1 : 0);
  }
  if (server) {
    load(doc, "server_config.json");
    char addr[96];
    memset(addr, 0xA5, sizeof(addr));
    CHECK(moonrakerParseSpoolmanServer(result(doc), addr, sizeof(addr)));
    CHECK_STR(addr, server);
  }
}

// ---- what is built from the object list in the tests below ------------------

static void namesFrom(const char* result_json) {
  JsonDocument doc;
  parseText(doc, result_json);
  memset(&g_names, 0, sizeof(g_names));
  CHECK(moonrakerParseObjectList(whole(doc), &g_names));
}

static bool rawFrom(const char* status_json) {
  JsonDocument doc;
  parseText(doc, status_json);
  memset(&g_raw, 0, sizeof(g_raw));
  return moonrakerParseQuery(whole(doc), g_names, &g_raw);
}

// ---- the cases --------------------------------------------------------------

static void caseHhPush8() {
  JsonDocument body;
  runCase("hh_push_8", body);
  expectNames(true, false, false, false, 0, 0);
  CHECK_STR(g_names.unknown_system, "");
  expectAsked(body, "mmu", "num_gates gate_spool_id gate_material gate_color gate spoolman_support");
  expectAsked(body, "print_stats", "state");
  expectAsked(body, "virtual_sdcard", "progress");
  expectNotAsked(body, "AFC");
  expectNotAsked(body, "toolchanger");

  CHECK(g_raw.has_mmu);
  CHECK(!g_raw.has_afc);
  CHECK_INT(g_raw.tool_count, 0);
  CHECK_INT(g_raw.mmu_mode, KL_MMU_PUSH);
  CHECK_INT(g_raw.mmu_count, 8);
  const int spools[8] = {5, 0, 7, 12, 0, 0, 9, 0};     // -1 in the answer is 0
  const char* const material[8] = {"PLA", "", "PETG", "PLA", "", "", "ABS", ""};
  // The colours as they came, the # off: "#00FF00" is stored as "00FF00", "ff8800" stays.
  const char* const color[8] = {"FF0000", "", "00FF00", "0000FF", "", "", "ff8800", ""};
  for (int i = 0; i < 8; i++) {
    CHECK_INT(g_raw.mmu[i].spool_id, spools[i]);
    CHECK_INT(g_raw.mmu[i].number, i);
    CHECK_INT(g_raw.mmu[i].loaded, i == 3 ? 1 : 0);
    CHECK_STR(g_raw.mmu[i].material, material[i]);
    CHECK_STR(g_raw.mmu[i].color, color[i]);
  }
  CHECK_INT(g_raw.active_spool, 12);

  CHECK_INT(g_facts.layout, KL_LAYOUT_MMU);
  CHECK_INT(g_facts.mmu_mode, KL_MMU_PUSH);
  CHECK_INT(g_facts.field_count, 8);
  CHECK(!g_facts.truncated);
  CHECK(!g_facts.tools_without_var);
  for (int i = 0; i < 8; i++) CHECK_INT(g_facts.field[i].spool_id, spools[i]);
  expectLoadedField(g_facts, 3);
  CHECK_INT(g_facts.active_spool, 12);
  CHECK_INT(klipperUnitCount(g_facts), 2);
  expectUnitLabel(g_facts, 0, 0, 4, "MMU 0-3");
  expectUnitLabel(g_facts, 1, 4, 4, "MMU 4-7");
  expectStatePercent("printing", 42);
  expectServer(true, true, true, "http://192.0.2.20:7912");
}

static void caseHhPull4() {
  JsonDocument body;
  runCase("hh_pull_4", body);
  expectNames(true, false, false, false, 0, 0);
  CHECK_INT(g_raw.mmu_mode, KL_MMU_PULL);
  CHECK_INT(g_raw.mmu_count, 4);
  const int spools[4] = {3, 4, 0, 8};
  const char* const material[4] = {"PLA", "PLA", "", "PETG"};
  const char* const color[4] = {"FFFFFF", "", "", "112233"};
  for (int i = 0; i < 4; i++) {
    CHECK_INT(g_raw.mmu[i].spool_id, spools[i]);
    CHECK_INT(g_raw.mmu[i].loaded, 0);               // "gate": -1, no gate is loaded
    CHECK_STR(g_raw.mmu[i].material, material[i]);
    CHECK_STR(g_raw.mmu[i].color, color[i]);
  }
  CHECK_INT(g_raw.active_spool, 0);                  // "spool_id": null

  CHECK_INT(g_facts.layout, KL_LAYOUT_MMU);
  CHECK_INT(g_facts.mmu_mode, KL_MMU_PULL);
  CHECK_INT(g_facts.field_count, 4);
  for (int i = 0; i < 4; i++) CHECK_INT(g_facts.field[i].spool_id, spools[i]);
  expectLoadedField(g_facts, -1);
  CHECK_INT(klipperUnitCount(g_facts), 1);
  expectUnitLabel(g_facts, 0, 0, 4, "MMU 0-3");
  expectStatePercent("standby", 0);
  // Spoolman is set up and not connected; its address has no scheme and comes as written.
  expectServer(true, true, false, "192.0.2.20:7912");
}

static void caseAfc2Units() {
  JsonDocument body;
  runCase("afc_2units", body);
  expectNames(false, true, false, false, 0, 7);
  // Lanes only, not the units or hubs that start with AFC_ as well.
  for (int i = 0; i < 7; i++) {
    char obj[64];
    snprintf(obj, sizeof(obj), "AFC_stepper lane%d", i + 1);
    CHECK_STR(g_names.lane_object[i], obj);
    expectAsked(body, obj, "spool_id material color unit tool_loaded");
  }
  expectAsked(body, "AFC", "lanes current_load");
  expectAsked(body, "print_stats", "state");
  expectAsked(body, "virtual_sdcard", "progress");
  expectNotAsked(body, "mmu");
  expectNotAsked(body, "toolchanger");
  expectNotAsked(body, "AFC_BoxTurtle Turtle_1");
  expectNotAsked(body, "AFC_hub Turtle_1");

  CHECK(g_raw.has_afc);
  CHECK(!g_raw.has_mmu);
  CHECK_INT(g_raw.afc_count, 7);
  CHECK_INT(g_raw.tool_count, 0);
  // lane3 has "spool_id": null and lane4 "": no spool.
  const int spools[7] = {5, 12, 0, 0, 31, 32, 33};
  const char* const material[7] = {"PLA", "PETG", "", "", "ASA", "PLA", "TPU"};
  const char* const color[7] = {"FF0000", "00FF00", "000000", "", "0000FF", "FFFFFF", "FFFF00"};
  for (int i = 0; i < 7; i++) {
    char lane[24];
    snprintf(lane, sizeof(lane), "lane%d", i + 1);
    CHECK_INT(g_raw.afc[i].spool_id, spools[i]);
    CHECK_STR(g_raw.afc[i].label, lane);
    CHECK_STR(g_raw.afc[i].unit, i < 4 ? "Turtle_1" : "Turtle_2");
    CHECK_STR(g_raw.afc[i].material, material[i]);
    CHECK_STR(g_raw.afc[i].color, color[i]);
    CHECK_INT(g_raw.afc[i].loaded, i == 1 ? 1 : 0);
  }
  CHECK_INT(g_raw.active_spool, 12);

  CHECK_INT(g_facts.layout, KL_LAYOUT_AFC);
  CHECK_INT(g_facts.field_count, 7);
  for (int i = 0; i < 7; i++) CHECK_INT(g_facts.field[i].spool_id, spools[i]);
  expectLoadedField(g_facts, 1);
  CHECK_INT(klipperUnitCount(g_facts), 2);
  expectUnitLabel(g_facts, 0, 0, 4, "Turtle_1");
  expectUnitLabel(g_facts, 1, 4, 3, "Turtle_2");
  expectStatePercent("paused", 50);
  expectServer(true, true, true, "http://spoolman.lan:7912/");
}

static void caseToolchanger4() {
  JsonDocument body;
  runCase("toolchanger_4", body);
  expectNames(false, false, true, true, 4, 0);
  for (int i = 0; i < 4; i++) {
    char macro[16];
    snprintf(macro, sizeof(macro), "T%d", i);
    CHECK_INT(g_names.macro_tool[i], i);
    CHECK_STR(g_names.macro_name[i], macro);
  }
  expectAsked(body, "toolchanger", "tool_number");
  // Each tool macro is asked for with no list of fields: a field it does not
  // have would come back as null all the same, so only the whole object says
  // which variables it really has.
  for (int i = 0; i < 4; i++) {
    char obj[32];
    snprintf(obj, sizeof(obj), "gcode_macro T%d", i);
    expectAskedFull(body, obj);
  }
  expectNotAsked(body, "mmu");
  expectNotAsked(body, "AFC");
  // On the wire: the name, then null.
  char wire[4096];
  serializeJson(body, wire, sizeof(wire));
  CHECK(strstr(wire, "\"gcode_macro T1\":null") != nullptr);

  CHECK_INT(g_raw.tool_count, 4);
  CHECK(g_raw.has_save_variables);
  CHECK_INT(g_raw.active_tool, 1);
  const int spools[4] = {21, 22, 0, 24};              // T2 has "spool_id": null
  for (int i = 0; i < 4; i++) {
    CHECK_INT(g_raw.tool[i].number, i);
    CHECK(g_raw.tool[i].has_var);
    CHECK_INT(g_raw.tool[i].spool_id, spools[i]);
  }
  CHECK_INT(g_raw.active_spool, 22);

  CHECK_INT(g_facts.layout, KL_LAYOUT_TOOLS);
  CHECK_INT(g_facts.field_count, 4);
  CHECK(!g_facts.tools_without_var);
  CHECK(g_facts.has_save_variables);
  for (int i = 0; i < 4; i++) CHECK_INT(g_facts.field[i].spool_id, spools[i]);
  expectLoadedField(g_facts, 1);
  CHECK_INT(klipperUnitCount(g_facts), 1);
  expectUnitLabel(g_facts, 0, 0, 4, "T0-T3");
  expectStatePercent("complete", 100);
  expectServer(true, true, true, "http://192.0.2.20:7912");
}

static void caseToolsNoVar() {
  JsonDocument body;
  runCase("tools_no_var", body);
  expectNames(false, false, false, false, 2, 0);
  expectAskedFull(body, "gcode_macro T0");
  expectAskedFull(body, "gcode_macro T1");
  expectNotAsked(body, "toolchanger");

  CHECK_INT(g_raw.tool_count, 2);
  // T0 answers with another variable (color) and T1 with none: neither has spool_id.
  CHECK(!g_raw.tool[0].has_var);
  CHECK(!g_raw.tool[1].has_var);
  CHECK_INT(g_raw.active_spool, 9);

  CHECK_INT(g_facts.layout, KL_LAYOUT_SINGLE);
  CHECK_INT(g_facts.field_count, 1);
  CHECK(g_facts.tools_without_var);
  CHECK_INT(g_facts.field[0].spool_id, 9);
  CHECK_INT(g_facts.active_spool, 9);
  expectStatePercent("standby", 0);
  expectServer(true, true, true, "http://192.0.2.20:7912");
}

static void caseSingle() {
  JsonDocument body;
  runCase("single", body);
  expectNames(false, false, false, false, 0, 0);
  CHECK_STR(g_names.unknown_system, "");
  CHECK_INT(g_raw.tool_count, 0);
  CHECK(!g_raw.has_mmu);
  CHECK(!g_raw.has_afc);

  CHECK_INT(g_facts.layout, KL_LAYOUT_SINGLE);
  CHECK_INT(g_facts.field_count, 1);
  CHECK(!g_facts.tools_without_var);
  CHECK_INT(g_facts.field[0].spool_id, 17);
  CHECK(g_facts.field[0].loaded);
  CHECK_STR(g_facts.unknown_system, "");
  expectStatePercent("cancelled", 12);
  expectServer(true, true, true, "http://192.0.2.20:7912");
}

static void caseCfsBox() {
  JsonDocument body;
  runCase("cfs_box", body);
  expectNames(false, false, false, false, 0, 0);
  CHECK_STR(g_names.unknown_system, "box");
  CHECK_INT(g_facts.layout, KL_LAYOUT_SINGLE);
  CHECK_INT(g_facts.field_count, 1);
  CHECK_STR(g_facts.unknown_system, "box");
  expectStatePercent("printing", 75);
  // No [spoolman] in this Moonraker: Klipper is ready, the component is not there,
  // and there is nothing more to ask.
  expectServer(false, false, false, nullptr);
}

// Jörg's real printer. The macros that start with T but are no tools stay out,
// and the null fields of mmu, AFC and toolchanger in the answer do not make a
// system: only the list of objects says what exists.
static void caseHulkSingle() {
  JsonDocument body;
  runCase("hulk_single", body);
  expectNames(false, false, false, true, 0, 0);
  CHECK_STR(g_names.unknown_system, "");
  // Not asked, since not listed: the answer in the file is of an older query.
  expectAsked(body, "print_stats", "state");
  expectAsked(body, "virtual_sdcard", "progress");
  expectNotAsked(body, "mmu");
  expectNotAsked(body, "AFC");
  expectNotAsked(body, "toolchanger");
  char wire[4096];
  serializeJson(body, wire, sizeof(wire));
  CHECK(strstr(wire, "mmu") == nullptr);
  CHECK(strstr(wire, "AFC") == nullptr);
  CHECK(strstr(wire, "toolchanger") == nullptr);
  CHECK(strstr(wire, "gcode_macro") == nullptr);       // no tool macros, and none of the others asked

  CHECK(!g_raw.has_mmu);
  CHECK(!g_raw.has_afc);
  CHECK_INT(g_raw.mmu_count, 0);
  CHECK_INT(g_raw.afc_count, 0);
  CHECK_INT(g_raw.tool_count, 0);
  CHECK(g_raw.has_save_variables);
  CHECK_INT(g_raw.active_spool, 14);

  CHECK_INT(g_facts.layout, KL_LAYOUT_SINGLE);
  CHECK_INT(g_facts.field_count, 1);
  CHECK_INT(g_facts.field[0].spool_id, 14);
  CHECK(g_facts.field[0].loaded);
  CHECK(g_facts.has_save_variables);
  CHECK(!g_facts.tools_without_var);
  CHECK(!g_facts.truncated);
  CHECK_STR(g_facts.unknown_system, "");
  CHECK_INT(klipperUnitCount(g_facts), 1);
  expectStatePercent("standby", 0);
  expectServer(true, true, true, "http://192.0.2.178:7912");
}

// A macro that is listed and answered with {"spool_id": null} after a query
// that named the field is no proof that it has the variable: Moonraker puts a
// requested field the object lacks in as null (TEST_FLOW below), and only the
// query without a list shows what is there (TEST_PA: nothing). So the macros
// are asked in full (the cases above pin that), and here a probe answer of
// the other kind is read with the names of the real printer: macros that are
// no tools make no tool, however they answer.
static void caseMacroProbe() {
  WHAT("a probe answer for macros that are no tools");
  g_case = "hulk_single";
  JsonDocument doc;
  load(doc, "objects_list.json");
  memset(&g_names, 0, sizeof(g_names));
  CHECK(moonrakerParseObjectList(result(doc), &g_names));
  CHECK_INT(g_names.macro_count, 0);

  // The trap in the file: asked for spool_id, TEST_FLOW answers {"spool_id": null}
  // as if it had the variable, and TEST_PA, asked for nothing in particular, {}.
  load(doc, "query_macro_probe.json");
  memset(&g_raw, 0, sizeof(g_raw));
  (void)moonrakerParseQuery(status(doc), g_names, &g_raw);
  CHECK_INT(g_raw.tool_count, 0);
  CHECK(!g_raw.has_mmu);
  CHECK(!g_raw.has_afc);
  klipperBuild(g_raw, &g_facts);
  CHECK_INT(g_facts.layout, KL_LAYOUT_SINGLE);
  CHECK(!g_facts.tools_without_var);
}

// The answer for the macros that are tools: the variable is there when the key
// spool_id is in the whole object, whatever its value.
static void caseMacroAnswers() {
  WHAT("tool macros: the key spool_id in the whole object");
  namesFrom("{\"objects\":[\"gcode_macro T0\",\"gcode_macro T1\",\"gcode_macro T2\",\"gcode_macro T3\","
            "\"gcode_macro T4\",\"toolchanger\"]}");
  CHECK_INT(g_names.macro_count, 5);
  CHECK(rawFrom("{\"toolchanger\":{\"tool_number\":2},"
                "\"gcode_macro T0\":{\"spool_id\":5,\"color\":\"ff0000\"},"
                "\"gcode_macro T1\":{\"spool_id\":null},"
                "\"gcode_macro T2\":{},"
                "\"gcode_macro T3\":{\"color\":\"00ff00\"}}"));    // T4 is not in the answer at all
  CHECK_INT(g_raw.tool_count, 5);
  CHECK(g_raw.tool[0].has_var);
  CHECK_INT(g_raw.tool[0].spool_id, 5);
  CHECK(g_raw.tool[1].has_var);                // null is a value of the variable
  CHECK_INT(g_raw.tool[1].spool_id, 0);
  CHECK(!g_raw.tool[2].has_var);
  CHECK(!g_raw.tool[3].has_var);
  CHECK(!g_raw.tool[4].has_var);
  CHECK_INT(g_raw.active_tool, 2);
  for (int i = 0; i < 5; i++) CHECK_INT(g_raw.tool[i].number, i);
}

// What the answer carries is never what the printer has.
static void caseAnswerMakesNoSystem() {
  WHAT("the answer of a Happy Hare read with the names of a plain printer");
  JsonDocument doc;
  loadFrom(doc, "single", "objects_list.json");
  memset(&g_names, 0, sizeof(g_names));
  CHECK(moonrakerParseObjectList(result(doc), &g_names));
  loadFrom(doc, "hh_push_8", "query.json");
  memset(&g_raw, 0, sizeof(g_raw));
  (void)moonrakerParseQuery(status(doc), g_names, &g_raw);
  CHECK(!g_raw.has_mmu);
  CHECK_INT(g_raw.mmu_count, 0);
  CHECK(!g_raw.has_afc);
  klipperBuild(g_raw, &g_facts);
  CHECK_INT(g_facts.layout, KL_LAYOUT_SINGLE);

  WHAT("the answer of a toolchanger read with the names of a plain printer");
  loadFrom(doc, "toolchanger_4", "query.json");
  memset(&g_raw, 0, sizeof(g_raw));
  (void)moonrakerParseQuery(status(doc), g_names, &g_raw);
  CHECK_INT(g_raw.tool_count, 0);
  klipperBuild(g_raw, &g_facts);
  CHECK_INT(g_facts.layout, KL_LAYOUT_SINGLE);
}

static void caseObjectList() {
  WHAT("object list: lanes, macros, and what only looks like them");
  namesFrom("{\"objects\":[\"AFC\",\"AFC_BoxTurtle Turtle_1\",\"AFC_hub Turtle_1\",\"AFC_lane lane1\","
            "\"AFC_lane lane2\",\"gcode_macro T0\",\"gcode_macro t5\",\"gcode_macro T01\",\"gcode_macro T10\","
            "\"gcode_macro T0_PREPARE\",\"gcode_macro TURN_OFF_HEATERS\",\"gcode_macro T\",\"gcode_macro _T1\","
            "\"tool T7\"]}");
  CHECK(g_names.has_afc);
  CHECK_INT(g_names.lane_count, 2);
  CHECK_STR(g_names.lane_object[0], "AFC_lane lane1");
  CHECK_STR(g_names.lane_object[1], "AFC_lane lane2");
  // R37: T<n> only, an upper-case T and the digits of the number without a
  // leading zero (T0 itself is a tool). "t5" and "T01" are macros of their own:
  // the commands name MACRO=T<n>, never t5 or T01. Nothing else that starts
  // with a T.
  CHECK_INT(g_names.macro_count, 2);
  CHECK_INT(g_names.macro_tool[0], 0);
  CHECK_STR(g_names.macro_name[0], "T0");
  CHECK_INT(g_names.macro_tool[1], 10);
  CHECK_STR(g_names.macro_name[1], "T10");
  CHECK(!g_names.has_mmu);
  CHECK(!g_names.has_toolchanger);
  CHECK(!g_names.has_save_variables);
  CHECK(!g_names.has_print_stats);
  CHECK(!g_names.has_virtual_sdcard);
  CHECK_STR(g_names.unknown_system, "");

  WHAT("object list: a printer with no print_stats gets no question about it");
  {
    JsonDocument body;
    namesFrom("{\"objects\":[\"extruder\",\"toolhead\"]}");
    moonrakerBuildQuery(g_names, body);
    expectBodyIsObjects(body);
    expectNotAsked(body, "print_stats");
    expectNotAsked(body, "virtual_sdcard");
    expectNotAsked(body, "mmu");
    expectNotAsked(body, "AFC");
    expectNotAsked(body, "toolchanger");
  }

  WHAT("object list: the closed boxes are an unknown system");
  namesFrom("{\"objects\":[\"webhooks\",\"box_stepper slot0\",\"box_stepper slot1\",\"box_stepper slot2\",\"box_stepper slot3\"]}");
  CHECK(g_names.unknown_system[0] != '\0');
  CHECK(!g_names.has_mmu);
  CHECK(!g_names.has_afc);
  namesFrom("{\"objects\":[\"webhooks\",\"box\"]}");
  CHECK_STR(g_names.unknown_system, "box");
  // A Happy Hare beside a box: the system it has comes first, the unknown one stays a note.
  namesFrom("{\"objects\":[\"mmu\",\"box\"]}");
  CHECK(g_names.has_mmu);

  WHAT("object list: no list is no printer");
  {
    JsonDocument doc;
    parseText(doc, "{}");
    KlipperObjectNames n;
    memset(&n, 0, sizeof(n));
    CHECK(!moonrakerParseObjectList(whole(doc), &n));
    parseText(doc, "null");
    CHECK(!moonrakerParseObjectList(whole(doc), &n));
  }
}

// ---- R15 and R19: what the parser keeps and what it counts --------------------

// The bytes of a struct hold the needle somewhere: a name that was meant to be
// left out has not been written anywhere, not even past the end of an array.
static bool bytesContain(const void* mem, size_t len, const char* needle) {
  const char* p = (const char*)mem;
  const size_t n = strlen(needle);
  for (size_t i = 0; i + n <= len; i++) {
    if (memcmp(p + i, needle, n) == 0) return true;
  }
  return false;
}

// R15: the name of a lane is written into "SET_SPOOL_ID LANE=<name>". A name with
// a space, a ';', a line break, a tab or a byte beyond ASCII, or one that does not
// fit the label (23 characters), would start another command or address another
// lane. Such a lane is skipped, not cut: it is not stored, not asked, and not
// counted (lane_count is the number of lanes the parser kept, however many
// there are, see caseMoreThanKept).
static void caseUnsafeLanes() {
  WHAT("unsafe lane names: only the safe one is stored");
  // One unsafe name comes before the good one, so that a skipped lane leaves no gap.
  namesFrom("{\"objects\":[\"AFC\",\"print_stats\",\"AFC_stepper a b\",\"AFC_stepper lane1\","
            "\"AFC_stepper a;b\",\"AFC_stepper a\\nb\",\"AFC_lane a\\tb\","
            "\"AFC_stepper abcdefghijklmnopqrstuvwx\",\"AFC_stepper lane\\u00e4\","
            "\"AFC_stepper \",\"AFC_lane \"]}");
  CHECK(g_names.has_afc);
  CHECK_INT(g_names.lane_count, 1);
  CHECK_STR(g_names.lane_object[0], "AFC_stepper lane1");
  for (int i = 1; i < COUNT(g_names.lane_object); i++) CHECK_INT(g_names.lane_object[i][0], 0);
  const char* const unsafe[] = {"a b", "a;b", "a\nb", "a\tb", "abcdefghijklmnopqrstuvwx", "\xC3\xA4"};
  for (int i = 0; i < COUNT(unsafe); i++) CHECK(!bytesContain(&g_names, sizeof(g_names), unsafe[i]));

  WHAT("unsafe lane names: the query asks for the safe one only");
  JsonDocument body;
  moonrakerBuildQuery(g_names, body);
  expectBodyIsObjects(body);
  expectAsked(body, "AFC", "lanes current_load");
  expectAsked(body, "AFC_stepper lane1", "spool_id material color unit tool_loaded");
  expectNotAsked(body, "AFC_stepper a b");
  expectNotAsked(body, "AFC_stepper a;b");
  expectNotAsked(body, "AFC_stepper a\nb");
  expectNotAsked(body, "AFC_lane a\tb");
  expectNotAsked(body, "AFC_stepper abcdefghijklmnopqrstuvwx");
  CHECK_INT(body.as<JsonVariantConst>()["objects"].size(), 3);     // AFC, lane1, print_stats
  char wire[1024];
  serializeJson(body, wire, sizeof(wire));
  CHECK(strstr(wire, "lane1") != nullptr);
  CHECK(strstr(wire, "a b") == nullptr);
  CHECK(strstr(wire, "a;b") == nullptr);
  CHECK(strstr(wire, "\\n") == nullptr);                           // an escaped line break
  CHECK(strstr(wire, "\\t") == nullptr);
  CHECK(strstr(wire, "uvwx") == nullptr);
  CHECK(strstr(wire, "\xC3\xA4") == nullptr);

  WHAT("unsafe lane names: an answer for them is not read, the command names the safe lane only");
  CHECK(rawFrom("{\"AFC\":{\"lanes\":[\"lane1\"],\"current_load\":\"lane1\"},"
                "\"AFC_stepper lane1\":{\"spool_id\":7,\"material\":\"PLA\",\"color\":\"#ff0000\",\"unit\":\"U1\",\"tool_loaded\":true},"
                "\"AFC_stepper a;b\":{\"spool_id\":9,\"material\":\"PETG\",\"color\":\"00ff00\",\"unit\":\"U1\",\"tool_loaded\":false},"
                "\"print_stats\":{\"state\":\"standby\"}}"));
  CHECK(g_raw.has_afc);
  CHECK_INT(g_raw.afc_count, 1);
  CHECK_STR(g_raw.afc[0].label, "lane1");
  CHECK_INT(g_raw.afc[0].spool_id, 7);
  CHECK_INT(g_raw.afc[0].loaded, 1);
  CHECK_STR(g_raw.afc[1].label, "");
  CHECK_INT(g_raw.afc[1].spool_id, 0);
  CHECK(!bytesContain(&g_raw, sizeof(g_raw), "a;b"));
  CHECK(!bytesContain(&g_raw, sizeof(g_raw), "PETG"));
  klipperBuild(g_raw, &g_facts);
  CHECK_INT(g_facts.layout, KL_LAYOUT_AFC);
  CHECK_INT(g_facts.field_count, 1);
  static KlipperPlan plan;                                         // about 4 kB: not on the stack
  CHECK(klipperPlanAssign(g_facts, 0, 12, &plan));
  CHECK_STR(plan.set, "SET_SPOOL_ID LANE=lane1 SPOOL_ID=12");
  CHECK_STR(plan.clear, "");

  WHAT("lane names: 23 characters fit the label, 24 do not");
  namesFrom("{\"objects\":[\"AFC\",\"AFC_stepper abcdefghijklmnopqrstuvw\",\"AFC_lane abcdefghijklmnopqrstuvwx\"]}");
  CHECK_INT(g_names.lane_count, 1);
  CHECK_STR(g_names.lane_object[0], "AFC_stepper abcdefghijklmnopqrstuvw");
  CHECK(rawFrom("{\"AFC_stepper abcdefghijklmnopqrstuvw\":{\"spool_id\":3}}"));
  CHECK_INT(g_raw.afc_count, 1);
  CHECK_STR(g_raw.afc[0].label, "abcdefghijklmnopqrstuvw");        // whole, not cut
  CHECK_INT(g_raw.afc[0].spool_id, 3);
}

// R19: the counts are the full number of lanes and tool macros found, saturating
// at 255, and the arrays keep the first KL_RAW_MAX of them (24). Whoever reads
// the arrays goes to min(count, KL_RAW_MAX): nothing beyond index 23 is stored,
// asked or read, and the neighbours of an array stay as they were.
static void caseMoreThanKept() {
  WHAT("30 lanes and 30 tool macros: full counts, the first 24 kept");
  std::string list = "{\"objects\":[\"AFC\",\"toolchanger\",\"print_stats\"";
  for (int i = 0; i < 30; i++) {
    list += ",\"AFC_stepper lane" + std::to_string(i + 1) + "\"";
    list += ",\"gcode_macro T" + std::to_string(i) + "\"";
  }
  list += "]}";
  namesFrom(list.c_str());
  CHECK_INT(g_names.lane_count, 30);
  CHECK_INT(g_names.macro_count, 30);
  for (int i = 0; i < KL_RAW_MAX; i++) {
    char lane[64];
    char macro[16];
    snprintf(lane, sizeof(lane), "AFC_stepper lane%d", i + 1);
    snprintf(macro, sizeof(macro), "T%d", i);
    CHECK_STR(g_names.lane_object[i], lane);
    CHECK_INT(g_names.macro_tool[i], i);
    CHECK_STR(g_names.macro_name[i], macro);
  }
  // The member behind lane_object, and the ones an overrun of macro_name would reach.
  CHECK_STR(g_names.unknown_system, "");
  CHECK(!bytesContain(&g_names, sizeof(g_names), "lane25"));
  CHECK(!bytesContain(&g_names, sizeof(g_names), "lane30"));
  CHECK(!bytesContain(&g_names, sizeof(g_names), "T24"));
  CHECK(!bytesContain(&g_names, sizeof(g_names), "T29"));

  WHAT("30 lanes and 30 tool macros: the query asks for the first 24 of each");
  JsonDocument body;
  moonrakerBuildQuery(g_names, body);
  expectBodyIsObjects(body);
  expectAsked(body, "AFC_stepper lane1", "spool_id");
  expectAsked(body, "AFC_stepper lane24", "spool_id material color unit tool_loaded");
  expectNotAsked(body, "AFC_stepper lane25");
  expectNotAsked(body, "AFC_stepper lane30");
  expectAskedFull(body, "gcode_macro T0");
  expectAskedFull(body, "gcode_macro T23");
  expectNotAsked(body, "gcode_macro T24");
  expectNotAsked(body, "gcode_macro T29");
  // AFC, toolchanger, print_stats, 24 lanes and 24 macros.
  CHECK_INT(body.as<JsonVariantConst>()["objects"].size(), 51);

  WHAT("30 lanes and 30 tool macros: the answer is read up to 24, the counts stay full");
  std::string answer = "{\"toolchanger\":{\"tool_number\":3},\"print_stats\":{\"state\":\"standby\"}";
  for (int i = 0; i < 30; i++) {
    // The lanes past the 24th carry a material no slot may ever hold.
    answer += ",\"AFC_stepper lane" + std::to_string(i + 1) + "\":{\"spool_id\":" + std::to_string(100 + i) +
              ",\"material\":\"" + (i < KL_RAW_MAX ? "PLA" : "OVERFLOW") + "\",\"unit\":\"U\"}";
    answer += ",\"gcode_macro T" + std::to_string(i) + "\":{\"spool_id\":" + std::to_string(300 + i) + "}";
  }
  answer += "}";
  CHECK(rawFrom(answer.c_str()));
  CHECK(g_raw.has_afc);
  CHECK_INT(g_raw.afc_count, 30);
  CHECK_INT(g_raw.tool_count, 30);
  CHECK_INT(g_raw.active_tool, 3);
  for (int i = 0; i < KL_RAW_MAX; i++) {
    char lane[24];
    snprintf(lane, sizeof(lane), "lane%d", i + 1);
    CHECK_STR(g_raw.afc[i].label, lane);
    CHECK_INT(g_raw.afc[i].spool_id, 100 + i);
    CHECK_STR(g_raw.afc[i].material, "PLA");
    CHECK_INT(g_raw.tool[i].number, i);
    CHECK(g_raw.tool[i].has_var);
    CHECK_INT(g_raw.tool[i].spool_id, 300 + i);
  }
  CHECK(!bytesContain(&g_raw, sizeof(g_raw), "OVERFLOW"));
  CHECK(!bytesContain(&g_raw, sizeof(g_raw), "lane25"));
  CHECK(!bytesContain(&g_raw, sizeof(g_raw), "lane30"));
  CHECK(!bytesContain(&g_raw, sizeof(g_raw), "T24"));
  CHECK(!bytesContain(&g_raw, sizeof(g_raw), "T29"));
  klipperBuild(g_raw, &g_facts);
  CHECK_INT(g_facts.layout, KL_LAYOUT_AFC);
  CHECK_INT(g_facts.field_count, KL_FIELDS_MAX);
  CHECK(g_facts.truncated);                                        // 30 lanes, 16 shown: only the full count says so

  WHAT("300 lanes and 300 tool macros: the counts saturate at 255");
  std::string many = "{\"objects\":[\"AFC\"";
  for (int i = 0; i < 300; i++) {
    many += ",\"AFC_stepper lane" + std::to_string(i + 1) + "\"";
    many += ",\"gcode_macro T" + std::to_string(i) + "\"";
  }
  many += "]}";
  namesFrom(many.c_str());
  CHECK_INT(g_names.lane_count, 255);
  CHECK_INT(g_names.macro_count, 255);
  CHECK_STR(g_names.lane_object[KL_RAW_MAX - 1], "AFC_stepper lane24");
  CHECK_STR(g_names.macro_name[KL_RAW_MAX - 1], "T23");
  CHECK_STR(g_names.unknown_system, "");
}

static void caseQuery() {
  WHAT("query: the modes of Happy Hare, the gate of the bypass, colours with alpha");
  namesFrom("{\"objects\":[\"mmu\",\"print_stats\",\"virtual_sdcard\"]}");
  CHECK(rawFrom("{\"mmu\":{\"num_gates\":2,\"gate_spool_id\":[4,-1],\"gate_material\":[\"PLA\",\"\"],"
                "\"gate_color\":[\"#ff8800cc\",\"\"],\"gate\":-2,\"spoolman_support\":\"readonly\"},"
                "\"print_stats\":{\"state\":\"printing\"},\"virtual_sdcard\":{\"progress\":1.0}}"));
  CHECK_INT(g_raw.mmu_mode, KL_MMU_READONLY);
  CHECK_INT(g_raw.mmu_count, 2);
  CHECK_INT(g_raw.mmu[0].spool_id, 4);
  CHECK_INT(g_raw.mmu[1].spool_id, 0);
  CHECK_INT(g_raw.mmu[0].loaded, 0);
  CHECK_INT(g_raw.mmu[1].loaded, 0);
  CHECK_STR(g_raw.mmu[0].color, "ff8800cc");
  CHECK_INT(g_raw.progress, 100);
  const std::string job = ",\"print_stats\":{\"state\":\"standby\"},\"virtual_sdcard\":{\"progress\":0.0}}";
  const std::string off = std::string("{\"mmu\":{\"num_gates\":1,\"gate_spool_id\":[4],\"gate_material\":[\"PLA\"],"
                                      "\"gate_color\":[\"\"],\"gate\":0,\"spoolman_support\":\"off\"}") + job;
  CHECK(rawFrom(off.c_str()));
  CHECK_INT(g_raw.mmu_mode, KL_MMU_OFF);
  CHECK_INT(g_raw.mmu[0].loaded, 1);
  const std::string odd = std::string("{\"mmu\":{\"num_gates\":1,\"gate_spool_id\":[4],\"gate_material\":[\"PLA\"],"
                                      "\"gate_color\":[\"\"],\"gate\":0,\"spoolman_support\":\"sometimes\"}") + job;
  CHECK(rawFrom(odd.c_str()));
  CHECK_INT(g_raw.mmu_mode, KL_MMU_UNKNOWN);

  WHAT("query: no answer at all");
  {
    JsonDocument doc;
    parseText(doc, "null");
    memset(&g_raw, 0, sizeof(g_raw));
    CHECK(!moonrakerParseQuery(whole(doc), g_names, &g_raw));
  }
}

static void caseServerAnswers() {
  WHAT("the active spool");
  {
    JsonDocument doc;
    parseText(doc, "{\"spool_id\":5}");
    CHECK_INT(moonrakerParseActiveSpool(whole(doc)), 5);
    parseText(doc, "{\"spool_id\":null}");
    CHECK_INT(moonrakerParseActiveSpool(whole(doc)), 0);
    parseText(doc, "{}");
    CHECK_INT(moonrakerParseActiveSpool(whole(doc)), 0);
    parseText(doc, "null");
    CHECK_INT(moonrakerParseActiveSpool(whole(doc)), 0);
  }

  WHAT("server info: Klipper's state and the component spoolman");
  {
    JsonDocument doc;
    char state[KL_STATE_MAX];
    bool has = true;
    parseText(doc, "{\"klippy_state\":\"startup\",\"components\":[\"database\",\"mmu_server\"]}");
    CHECK(moonrakerParseServerInfo(whole(doc), state, sizeof(state), &has));
    CHECK_STR(state, "startup");
    CHECK(!has);                                    // mmu_server is not spoolman
    has = false;
    parseText(doc, "{\"klippy_state\":\"error\",\"components\":[\"spoolman\"]}");
    CHECK(moonrakerParseServerInfo(whole(doc), state, sizeof(state), &has));
    CHECK_STR(state, "error");
    CHECK(has);
  }

  WHAT("spoolman status");
  {
    JsonDocument doc;
    bool up = false;
    parseText(doc, "{\"spoolman_connected\":true,\"pending_reports\":[]}");
    CHECK(moonrakerParseSpoolmanStatus(whole(doc), &up));
    CHECK(up);
    up = true;
    parseText(doc, "{\"spoolman_connected\":false}");
    CHECK(moonrakerParseSpoolmanStatus(whole(doc), &up));
    CHECK(!up);
  }

  WHAT("spoolman address: the evaluated value, else the one from the file");
  {
    JsonDocument doc;
    char addr[96];
    parseText(doc, "{\"config\":{\"spoolman\":{\"server\":\"http://a:7912\"}},\"orig\":{\"spoolman\":{\"server\":\"b:1/\"}}}");
    CHECK(moonrakerParseSpoolmanServer(whole(doc), addr, sizeof(addr)));
    CHECK_STR(addr, "http://a:7912");
    parseText(doc, "{\"config\":{},\"orig\":{\"spoolman\":{\"server\":\"b:1/\"}}}");
    CHECK(moonrakerParseSpoolmanServer(whole(doc), addr, sizeof(addr)));
    CHECK_STR(addr, "b:1/");
    parseText(doc, "{\"config\":{\"server\":{\"port\":7125}},\"orig\":{}}");
    CHECK(!moonrakerParseSpoolmanServer(whole(doc), addr, sizeof(addr)));
  }

  WHAT("the newest spool of a Spoolman: a bare list from Spoolman, or the proxy's answer");
  {
    JsonDocument doc;
    int id = -9;
    char registered[40];
    memset(registered, 0xA5, sizeof(registered));
    parseText(doc, "[{\"id\":87,\"registered\":\"2026-10-20T08:15:30.123456Z\",\"remaining_weight\":812.5}]");
    CHECK(moonrakerParseNewestSpool(whole(doc), &id, registered, sizeof(registered)));
    CHECK_INT(id, 87);
    CHECK_STR(registered, "2026-10-20T08:15:30.123456Z");
    // Asked with limit=1 and sorted by number, descending; the first is the newest.
    parseText(doc, "[{\"id\":87,\"registered\":\"2026-10-20T08:15:30Z\"},{\"id\":86,\"registered\":\"2026-10-19T08:00:00Z\"}]");
    CHECK(moonrakerParseNewestSpool(whole(doc), &id, registered, sizeof(registered)));
    CHECK_INT(id, 87);
    CHECK_STR(registered, "2026-10-20T08:15:30Z");
    // Through Moonraker's proxy the list sits in result.response (v2 answer).
    parseText(doc, "{\"result\":{\"response\":[{\"id\":41,\"registered\":\"2026-01-02T03:04:05Z\"}],\"error\":null}}");
    CHECK(moonrakerParseNewestSpool(doc.as<JsonVariantConst>()["result"]["response"], &id, registered, sizeof(registered)));
    CHECK_INT(id, 41);
    CHECK_STR(registered, "2026-01-02T03:04:05Z");
    // A Spoolman with no spool: the list is empty, which is an answer.
    id = -9;
    parseText(doc, "[]");
    CHECK(moonrakerParseNewestSpool(whole(doc), &id, registered, sizeof(registered)));
    CHECK_INT(id, 0);
    // Not a list: no answer.
    parseText(doc, "{\"detail\":\"Not Found\"}");
    CHECK(!moonrakerParseNewestSpool(whole(doc), &id, registered, sizeof(registered)));
    parseText(doc, "null");
    CHECK(!moonrakerParseNewestSpool(whole(doc), &id, registered, sizeof(registered)));
  }
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: t <test/fixtures/moonraker>\n");
    return 2;
  }
  g_dir = argv[1];
  caseHhPush8();
  caseHhPull4();
  caseAfc2Units();
  caseToolchanger4();
  caseToolsNoVar();
  caseSingle();
  caseCfsBox();
  caseHulkSingle();
  caseMacroProbe();
  caseMacroAnswers();
  caseAnswerMakesNoSystem();
  caseObjectList();
  caseUnsafeLanes();
  caseMoreThanKept();
  caseQuery();
  caseServerAnswers();
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    # ArduinoJson as a system include: its own headers are not held to -Wextra.
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    "-isystem", str(arduinojson),
                    str(cpp),
                    str(root / "src" / "services" / "moonraker_parse.cpp"),
                    str(root / "src" / "services" / "klipper_rules.cpp"),
                    str(root / "src" / "services" / "klipper_test_rules.cpp"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary), str(fixtures)], check=True)

# The step in the CI file: in the build job, after "Build", for the one board
# whose libraries it reads (the folder is there once the build has fetched them).
ci = (root / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
step = "python3 test/test_moonraker_parse.py .pio/libdeps/wt32-sc01-plus/ArduinoJson/src"
assert step in ci, f".github/workflows/ci.yml: no step runs '{step}'"
at = ci.index(step)
assert ci.rfind("- name: Build\n", 0, at) >= 0 and ci.rfind("run: pio run -e ${{ matrix.env }}", 0, at) >= 0, \
    ".github/workflows/ci.yml: the moonraker parse step does not come after the Build step"
head = ci[ci.rfind("- name:", 0, at):at]
assert "if: matrix.env == 'wt32-sc01-plus'" in head, \
    ".github/workflows/ci.yml: the moonraker parse step does not run for wt32-sc01-plus only"
print("test_moonraker_parse: ok")
