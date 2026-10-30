#include "services/moonraker_parse.h"

#include <string.h>

namespace {

const char MACRO_PREFIX[] = "gcode_macro ";
const char STEPPER_PREFIX[] = "AFC_stepper ";
const char LANE_PREFIX[] = "AFC_lane ";
const char BOX_STEPPER_PREFIX[] = "box_stepper ";

// What the query asks of each object (R6: only objects the list named).
const char* const MMU_FIELDS[] = {"num_gates", "gate_spool_id", "gate_material",
                                  "gate_color", "gate", "spoolman_support"};
const char* const AFC_FIELDS[] = {"lanes", "current_load"};
const char* const LANE_FIELDS[] = {"spool_id", "material", "color", "unit", "tool_loaded"};
const char* const TOOLCHANGER_FIELDS[] = {"tool_number"};
const char* const STATE_FIELDS[] = {"state"};
const char* const PROGRESS_FIELDS[] = {"progress"};

// ------------------------------------------------------------
//  Small text and value helpers. No snprintf and no String: a text that
//  does not fit is cut or refused here, and nothing can raise a truncation
//  warning.
// ------------------------------------------------------------

// Copies src into dst (cap bytes) and always terminates. A text that does not
// fit is cut on a UTF-8 boundary. A null is "".
void copyText(char* dst, size_t cap, const char* src) {
  if (cap == 0) return;
  size_t n = src ? strlen(src) : 0;
  if (n >= cap) {
    n = cap - 1;
    // src[n] is the first byte left out. A byte of the form 10xxxxxx continues
    // a character, so the cut would fall inside one: back up to its start.
    while (n > 0 && (static_cast<unsigned char>(src[n]) & 0xC0) == 0x80) n--;
  }
  if (n > 0) memcpy(dst, src, n);
  dst[n] = '\0';
}

// Copies src whole or not at all: false, and dst is "", when it does not fit.
bool copyWhole(char* dst, size_t cap, const char* src) {
  if (cap == 0) return false;
  const size_t n = strlen(src);
  if (n >= cap) {
    dst[0] = '\0';
    return false;
  }
  memcpy(dst, src, n + 1);
  return true;
}

// The rest of `name` behind `prefix`, or null when it does not start with it.
const char* after(const char* name, const char* prefix) {
  const size_t n = strlen(prefix);
  return strncmp(name, prefix, n) == 0 ? name + n : nullptr;
}

// The text of a JSON value; "" for null, a number or a missing key.
const char* textOf(JsonVariantConst v) {
  const char* s = v.as<const char*>();
  return s ? s : "";
}

// A colour as it came, without the '#'. ("FF0000" and "#FF0000" are the same
// colour to the printers; the rest of the firmware wants it bare.)
void copyColor(char* dst, size_t cap, JsonVariantConst v) {
  const char* s = textOf(v);
  if (s[0] == '#') s++;
  copyText(dst, cap, s);
}

// A spool number from a value that may be a number, a string of digits, null
// or an empty string (AFC writes "" for a lane with no spool, Happy Hare -1).
// Anything that is not a positive whole number is 0, which is "no spool".
int spoolIdOf(JsonVariantConst v) {
  if (v.is<int>()) {
    const int id = v.as<int>();
    return id > 0 ? id : 0;
  }
  const char* s = v.as<const char*>();
  if (!s) return 0;
  int id = 0;
  int digits = 0;
  for (; *s >= '0' && *s <= '9' && digits < 9; s++, digits++) id = id * 10 + (*s - '0');
  return *s == '\0' ? id : 0;   // "12x" and ten digits are no number
}

bool listHas(JsonVariantConst list, const char* name) {
  for (JsonVariantConst v : list.as<JsonArrayConst>()) {
    const char* s = v.as<const char*>();
    if (s && strcmp(s, name) == 0) return true;
  }
  return false;
}

// The entries of an array the arrays below keep: never more than KL_RAW_MAX.
int keptOf(uint8_t count) {
  return count < KL_RAW_MAX ? count : KL_RAW_MAX;
}

// virtual_sdcard.progress, 0..1, as whole percent (rounded).
int8_t percentOf(double fraction) {
  if (fraction < 0.0) fraction = 0.0;
  if (fraction > 1.0) fraction = 1.0;
  return static_cast<int8_t>(fraction * 100.0 + 0.5);
}

// ------------------------------------------------------------
//  The list of objects
// ------------------------------------------------------------

// Root (R37): the commands name a tool macro as MACRO=T<n> and the saved
// variable as t<n>__spool_id, both written from the number (klipper_rules.cpp).
// A macro the parser took for tool 1 but that is spelled "t1" or "T01" would be
// addressed as T1, which is another macro, or none.
// Fix: "T5" is the tool 5: 5, "T0" the tool 0. The T is upper case, as in
// Mainsail's `gcode_macro T\d+`, and the digits have no leading zero (stricter
// than that pattern on purpose). Everything else that starts with a T ("t5",
// "T01", "T0_PREPARE", "TURN_OFF_HEATERS", a bare "T") is no tool: -1. At most
// four digits, so a number always fits the int16_t it is kept in.
// NOT-TO-DO: keeping the printer's spelling and writing the commands with it:
// the commands the scale may send are a fixed list.
int toolNumberOf(const char* macro) {
  if (macro[0] != 'T') return -1;
  const char* d = macro + 1;
  if (d[0] == '0' && d[1] != '\0') return -1;   // "T01", "T00": T0 alone is the tool 0
  int len = 0;
  int number = 0;
  for (; d[len] >= '0' && d[len] <= '9'; len++) {
    if (len >= 4) return -1;
    number = number * 10 + (d[len] - '0');
  }
  return (len > 0 && d[len] == '\0') ? number : -1;
}

// R15: a name the printer supplies and that is later written into G-code may
// hold nothing but letters, digits, '_', '.' and '-'. A space, a newline or a
// ';' in a lane name would end the command and start another one.
bool isSafeName(const char* s) {
  if (s[0] == '\0') return false;
  for (; *s != '\0'; s++) {
    const char c = *s;
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '_' || c == '.' || c == '-';
    if (!ok) return false;
  }
  return true;
}

// The lane's own name behind the object's section name, or null when the
// object is no lane. AFC calls its lanes AFC_stepper (older) or AFC_lane
// (newer); the units (AFC_BoxTurtle ...) and hubs (AFC_hub ...) are neither.
const char* laneNameOf(const char* object) {
  const char* lane = after(object, STEPPER_PREFIX);
  return lane ? lane : after(object, LANE_PREFIX);
}

// The objects whose mere presence is the fact. False for any other object.
bool noteFlag(KlipperObjectNames* o, const char* name) {
  bool* flag = nullptr;
  if (strcmp(name, "mmu") == 0) flag = &o->has_mmu;
  else if (strcmp(name, "AFC") == 0) flag = &o->has_afc;
  else if (strcmp(name, "toolchanger") == 0) flag = &o->has_toolchanger;
  else if (strcmp(name, "save_variables") == 0) flag = &o->has_save_variables;
  else if (strcmp(name, "print_stats") == 0) flag = &o->has_print_stats;
  else if (strcmp(name, "virtual_sdcard") == 0) flag = &o->has_virtual_sdcard;
  if (flag) *flag = true;
  return flag != nullptr;
}

// A tool macro. The name is T<digits> by construction (toolNumberOf), so it is
// safe for G-code (R15) and fits macro_name; the count goes on past
// KL_RAW_MAX, the arrays do not.
void noteMacro(KlipperObjectNames* o, const char* macro) {
  const int tool = toolNumberOf(macro);
  if (tool < 0) return;
  if (o->macro_count < KL_RAW_MAX) {
    o->macro_tool[o->macro_count] = static_cast<int16_t>(tool);
    copyText(o->macro_name[o->macro_count], sizeof(o->macro_name[0]), macro);
  }
  if (o->macro_count < 255) o->macro_count++;
}

// Root: the lane's name is taken from the object list and written into
//   SET_SPOOL_ID LANE=<name>. A printer is free to name a section anything, so
//   a name with a space or a ';' would put a second command into the line, and
//   one longer than the label does not survive a cut (a lane "lane1" cut from
//   "lane10" addresses another lane).
// Fix: a lane whose name is not [A-Za-z0-9_.-]+ or does not fit the label
//   (KL_LANE_MAX) or the object name (lane_object) is left out: not stored,
//   not counted, not asked. Telling the user is the glue's job (it logs).
// NOT-TO-DO: cutting the name or replacing the odd characters. Either gives a
//   name that is another lane's, and the command would reach it.
// See test_moonraker_parse.py "object list: lanes, macros, and what only looks
//   like them".
void noteLane(KlipperObjectNames* o, const char* object, const char* lane) {
  if (!isSafeName(lane) || strlen(lane) >= KL_LANE_MAX || strlen(object) >= sizeof(o->lane_object[0])) return;
  if (o->lane_count < KL_RAW_MAX) copyText(o->lane_object[o->lane_count], sizeof(o->lane_object[0]), object);
  if (o->lane_count < 255) o->lane_count++;
}

// A system we cannot drive: kept as a note, the first one seen.
void noteUnknown(KlipperObjectNames* o, const char* what) {
  if (o->unknown_system[0] == '\0') copyText(o->unknown_system, sizeof(o->unknown_system), what);
}

void noteObject(KlipperObjectNames* o, const char* name) {
  if (noteFlag(o, name)) return;
  const char* macro = after(name, MACRO_PREFIX);
  if (macro) {
    noteMacro(o, macro);
    return;
  }
  const char* lane = laneNameOf(name);
  if (lane) {
    noteLane(o, name, lane);
    return;
  }
  if (strcmp(name, "box") == 0) noteUnknown(o, "box");
  else if (after(name, BOX_STEPPER_PREFIX)) noteUnknown(o, "box_stepper");
}

// ------------------------------------------------------------
//  The query
// ------------------------------------------------------------

template <size_t N>
void askFor(JsonObject objects, const char* object, const char* const (&fields)[N]) {
  JsonArray list = objects[object].to<JsonArray>();
  for (size_t i = 0; i < N; i++) list.add(fields[i]);
}

// "gcode_macro T1": the macro's object name. False when it does not fit.
bool macroObject(char* dst, size_t cap, const char* macro) {
  const size_t head = strlen(MACRO_PREFIX);
  const size_t n = strlen(macro);
  if (head + n + 1 > cap) {
    if (cap > 0) dst[0] = '\0';
    return false;
  }
  memcpy(dst, MACRO_PREFIX, head);
  memcpy(dst + head, macro, n + 1);
  return true;
}

// "T<n>", the label of a tool slot (the commands are written from the number).
void toolLabel(char* dst, size_t cap, int number) {
  char rev[8];
  int len = 0;
  int v = number < 0 ? 0 : number;
  do {
    rev[len++] = static_cast<char>('0' + v % 10);
    v /= 10;
  } while (v > 0 && len < 6);
  if (cap < static_cast<size_t>(len) + 2) {
    if (cap > 0) dst[0] = '\0';
    return;
  }
  dst[0] = 'T';
  for (int i = 0; i < len; i++) dst[1 + i] = rev[len - 1 - i];
  dst[1 + len] = '\0';
}

// ------------------------------------------------------------
//  Reading the answer of the query
// ------------------------------------------------------------

void readMmu(JsonVariantConst mmu, KlipperRaw* out) {
  out->has_mmu = true;
  out->mmu_mode = klipperMmuMode(mmu["spoolman_support"].as<const char*>());
  JsonVariantConst gates_v = mmu["num_gates"];
  int gates = gates_v.is<int>() ? gates_v.as<int>() : 0;   // null: no Happy Hare in the answer
  if (gates < 0) gates = 0;
  if (gates > 255) gates = 255;
  out->mmu_count = static_cast<uint8_t>(gates);

  // "gate" is the gate in the extruder; -1 is none and -2 the bypass.
  JsonVariantConst gate_v = mmu["gate"];
  const int loaded = gate_v.is<int>() ? gate_v.as<int>() : -1;

  JsonVariantConst ids = mmu["gate_spool_id"];
  JsonVariantConst materials = mmu["gate_material"];
  JsonVariantConst colors = mmu["gate_color"];
  for (int i = 0; i < gates && i < KL_RAW_MAX; i++) {
    KlipperRawSlot& s = out->mmu[i];
    s.number = static_cast<int16_t>(i);
    s.spool_id = spoolIdOf(ids[i]);
    s.loaded = (i == loaded);
    copyText(s.material, sizeof(s.material), textOf(materials[i]));
    copyColor(s.color, sizeof(s.color), colors[i]);
  }
}

void readLanes(JsonVariantConst status, const KlipperObjectNames& n, KlipperRaw* out) {
  out->has_afc = true;
  out->afc_count = n.lane_count;
  const int kept = keptOf(n.lane_count);
  for (int i = 0; i < kept; i++) {
    const char* object = n.lane_object[i];
    const char* lane = laneNameOf(object);
    JsonVariantConst fields = status[object];
    KlipperRawSlot& s = out->afc[i];
    s.number = static_cast<int16_t>(i);
    copyText(s.label, sizeof(s.label), lane ? lane : "");
    copyText(s.unit, sizeof(s.unit), textOf(fields["unit"]));
    copyText(s.material, sizeof(s.material), textOf(fields["material"]));
    copyColor(s.color, sizeof(s.color), fields["color"]);
    s.spool_id = spoolIdOf(fields["spool_id"]);
    JsonVariantConst in_tool = fields["tool_loaded"];
    s.loaded = in_tool.is<bool>() && in_tool.as<bool>();
  }
}

// Root: Moonraker answers a query for a field an object lacks with null, and
//   for an object that does not exist with null fields (read on Joerg's Voron
//   "Hulk", Moonraker v0.11.0-4-gb559b9a, 29.10.2026: gcode_macro TEST_FLOW
//   asked for spool_id gives {"spool_id": null}, mmu gives {"num_gates": null}).
//   So neither "the answer has the key" nor "the value is not null" says that a
//   macro has the variable or that a system is there.
// Fix: whether a system exists comes only from the list of objects (the
//   KlipperObjectNames flags, never a key of the answer). The tool macros are
//   asked with null, which returns every variable the macro really has, and
//   has_var is "the key spool_id is in that whole object"; its value may be
//   null, which is the spool 0.
// NOT-TO-DO: asking the macros for [spool_id] and reading "a key is there".
//   Every macro would count as a tool with the variable, TURN_OFF_HEATERS and
//   TEST_FLOW among them, and SET_GCODE_VARIABLE would fail on them.
// See test_moonraker_parse.py cases hulk_single, "a probe answer for macros
//   that are no tools" and "the answer of a Happy Hare read with the names of
//   a plain printer".
void readTools(JsonVariantConst status, const KlipperObjectNames& n, KlipperRaw* out) {
  out->tool_count = n.macro_count;
  const int kept = keptOf(n.macro_count);
  for (int i = 0; i < kept; i++) {
    KlipperRawSlot& s = out->tool[i];
    s.number = n.macro_tool[i];
    toolLabel(s.label, sizeof(s.label), s.number);
    // An answer that does not hold the macro, or holds it as no object, has
    // no spool_id key either: member lookup on such a value is unbound.
    JsonVariantConst var;
    char object[32];
    if (macroObject(object, sizeof(object), n.macro_name[i])) {
      const char* key = object;
      var = status[key]["spool_id"];
    }
    s.has_var = !var.isUnbound();
    s.spool_id = spoolIdOf(var);
  }
  if (n.has_toolchanger) {
    // toolchanger.tool_number is -1 with no tool selected.
    JsonVariantConst tool = status["toolchanger"]["tool_number"];
    const int number = tool.is<int>() ? tool.as<int>() : -1;
    if (number >= 0 && number <= 127) out->active_tool = static_cast<int8_t>(number);
  }
}

void readJob(JsonVariantConst status, KlipperRaw* out) {
  copyText(out->state, sizeof(out->state), textOf(status["print_stats"]["state"]));
  JsonVariantConst progress = status["virtual_sdcard"]["progress"];
  if (progress.is<double>()) out->progress = percentOf(progress.as<double>());
}

}  // namespace

// ------------------------------------------------------------
//  Public
// ------------------------------------------------------------

bool moonrakerParseObjectList(JsonVariantConst result, KlipperObjectNames* out) {
  memset(out, 0, sizeof(*out));
  JsonVariantConst objects = result["objects"];
  if (!objects.is<JsonArrayConst>()) return false;
  for (JsonVariantConst entry : objects.as<JsonArrayConst>()) {
    const char* name = entry.as<const char*>();
    if (name) noteObject(out, name);
  }
  return true;
}

void moonrakerBuildQuery(const KlipperObjectNames& n, JsonDocument& body) {
  JsonObject root = body.to<JsonObject>();
  JsonObject objects = root["objects"].to<JsonObject>();
  if (n.has_mmu) askFor(objects, "mmu", MMU_FIELDS);
  if (n.has_afc) {
    askFor(objects, "AFC", AFC_FIELDS);
    const int kept = keptOf(n.lane_count);
    for (int i = 0; i < kept; i++) {
      const char* lane = n.lane_object[i];
      askFor(objects, lane, LANE_FIELDS);
    }
  }
  if (n.has_toolchanger) askFor(objects, "toolchanger", TOOLCHANGER_FIELDS);
  // The tool macros with null: all fields (R6, see readTools).
  const int macros = keptOf(n.macro_count);
  for (int i = 0; i < macros; i++) {
    char object[32];
    if (!macroObject(object, sizeof(object), n.macro_name[i])) continue;
    const char* key = object;
    (void)objects[key].to<JsonVariant>();
  }
  if (n.has_print_stats) askFor(objects, "print_stats", STATE_FIELDS);
  if (n.has_virtual_sdcard) askFor(objects, "virtual_sdcard", PROGRESS_FIELDS);
}

bool moonrakerParseQuery(JsonVariantConst status, const KlipperObjectNames& n, KlipperRaw* out) {
  memset(out, 0, sizeof(*out));
  out->active_tool = -1;
  out->progress = -1;
  if (!status.is<JsonObjectConst>()) return false;
  out->has_save_variables = n.has_save_variables;
  copyText(out->unknown_system, sizeof(out->unknown_system), n.unknown_system);
  if (n.has_mmu) readMmu(status["mmu"], out);
  if (n.has_afc) readLanes(status, n, out);
  readTools(status, n, out);
  readJob(status, out);
  return true;
}

int moonrakerParseActiveSpool(JsonVariantConst result) {
  return spoolIdOf(result["spool_id"]);
}

bool moonrakerParseServerInfo(JsonVariantConst result, char* klippy_state, size_t n, bool* has_spoolman) {
  if (!klippy_state || n == 0 || !has_spoolman) return false;
  klippy_state[0] = '\0';
  *has_spoolman = false;
  const char* state = result["klippy_state"].as<const char*>();
  if (!state) return false;
  copyText(klippy_state, n, state);
  // Exactly "spoolman": Happy Hare's "mmu_server" is another component.
  *has_spoolman = listHas(result["components"], "spoolman");
  return true;
}

bool moonrakerParseSpoolmanStatus(JsonVariantConst result, bool* connected) {
  if (!connected) return false;
  *connected = false;
  JsonVariantConst up = result["spoolman_connected"];
  if (!up.is<bool>()) return false;
  *connected = up.as<bool>();
  return true;
}

bool moonrakerParseSpoolmanServer(JsonVariantConst result, char* out, size_t n) {
  if (!out || n == 0) return false;
  out[0] = '\0';
  const char* evaluated = result["config"]["spoolman"]["server"].as<const char*>();
  const char* from_file = result["orig"]["spoolman"]["server"].as<const char*>();
  const char* pick = (evaluated && evaluated[0] != '\0') ? evaluated : from_file;
  if (!pick || pick[0] == '\0') return false;
  return copyWhole(out, n, pick);
}

bool moonrakerParseNewestSpool(JsonVariantConst spools, int* id, char* registered, size_t n) {
  if (!id || !registered || n == 0) return false;
  *id = -1;
  registered[0] = '\0';
  if (!spools.is<JsonArrayConst>()) return false;
  if (spools.as<JsonArrayConst>().size() == 0) {
    *id = 0;
    return true;
  }
  JsonVariantConst newest = spools[0];
  const int number = spoolIdOf(newest["id"]);
  if (number <= 0) return false;
  if (!copyWhole(registered, n, textOf(newest["registered"]))) return false;
  *id = number;
  return true;
}
