#pragma once

#include <stddef.h>
#include <stdint.h>

#include <ArduinoJson.h>

#include "services/klipper_rules.h"

// ============================================================
//  MOONRAKER'S ANSWERS, READ INTO THE RAW FACTS
//
//  V46/V48 (Spec 5.3 Z, "docs/entwurf/2026-10-29-z-klipper.md"): the code
//  that talks to Moonraker (moonraker_api) fetches JSON; this unit turns it
//  into the plain structs of klipper_rules.h. It knows nothing of HTTP,
//  Arduino, logging or LVGL: a host test (test_moonraker_parse.py) feeds it
//  the answers in test/fixtures/moonraker/ and holds it to the values there.
//
//  The order of a read, and what each step hands to the next:
//
//    GET  /printer/objects/list   moonrakerParseObjectList  -> KlipperObjectNames
//    POST /printer/objects/query  moonrakerBuildQuery       <- KlipperObjectNames
//                                 moonrakerParseQuery       -> KlipperRaw
//    GET  /server/spoolman/spool_id   moonrakerParseActiveSpool
//
//  Every function takes the "result" of Moonraker's {"result": ...} body, and
//  moonrakerParseQuery its "result"."status". The caller unwraps; a body with
//  no "result" is the caller's error.
//
//  Strings are copied bounded and always terminated. A text that is only
//  shown (material, unit name) is cut on a UTF-8 boundary when it does not
//  fit. A text that ends up in G-code (a lane name, a tool macro name) is
//  never cut: it is left out (R15, see moonraker_parse.cpp).
// ============================================================

// What the printer has, from the list of its objects. This is the only
// source for the existence of a system (R6): a query answers for a missing
// object with null fields, so nothing in an answer says that something is
// there.
//
// Counts. macro_count and lane_count are the FULL number found, saturating at
// 255, like the counts of KlipperRaw; only the first KL_RAW_MAX entries are
// kept in the arrays. Whoever indexes macro_tool, macro_name or lane_object
// goes to min(count, KL_RAW_MAX), never to the count.
//
//   macro_tool/name  gcode_macro T<n> or t<n>: the number, and the name as the
//                    printer spells it ("T1" or "t1")
//   lane_object      the whole object name of an AFC lane ("AFC_stepper lane1"
//                    or "AFC_lane lane1"), the key of the query
//   unknown_system   a system we cannot drive ("box" of the Creality CFS,
//                    "box_stepper" of the QIDI Box), "" when there is none;
//                    only a note, the other systems keep their flags
struct KlipperObjectNames {
  bool has_mmu;
  bool has_afc;
  bool has_toolchanger;
  bool has_save_variables;
  bool has_print_stats;
  bool has_virtual_sdcard;
  uint8_t macro_count;
  int16_t macro_tool[KL_RAW_MAX];
  char macro_name[KL_RAW_MAX][16];
  uint8_t lane_count;
  char lane_object[KL_RAW_MAX][48];
  char unknown_system[KL_LANE_MAX];
};

// `result` is the answer of /printer/objects/list ({"objects": [...]}). `out`
// is written whole. False when there is no list of objects: then nothing is
// known about the printer.
bool moonrakerParseObjectList(JsonVariantConst result, KlipperObjectNames* out);

// The body of POST /printer/objects/query: {"objects": {...}}. Only objects
// the list named are asked for. `body` is cleared first.
//   mmu               the fields of Happy Hare
//   AFC and its lanes lanes, current_load, and per lane the five fields
//   toolchanger       tool_number
//   gcode_macro T<n>  null: all fields (see moonraker_parse.cpp, R6)
//   print_stats       state
//   virtual_sdcard    progress
void moonrakerBuildQuery(const KlipperObjectNames& n, JsonDocument& body);

// `status` is the "status" of the query's answer. `out` is written whole and
// is a caller-owned struct (about 5.8 kB): the functions here never keep one
// on the stack. False when the answer has no status object.
//   Happy Hare   gate_spool_id -1 is 0; a gate below 0 means no gate loaded
//   colours      as they come ("FF0000" or "#FF0000"), stored without the '#'
//   lanes        spool_id null or empty is 0
//   tool macros  has_var is "the key spool_id is in the macro's whole object",
//                a null value is spool 0
//   progress     0..1 becomes percent, -1 when the printer does not say
// Which systems there are comes from `n`, never from a key of the answer.
// active_spool is not in the answer; the caller sets it from
// moonrakerParseActiveSpool.
bool moonrakerParseQuery(JsonVariantConst status, const KlipperObjectNames& n, KlipperRaw* out);

// {"spool_id": 5} is 5; null, a missing key and anything that is no positive
// number are 0.
int moonrakerParseActiveSpool(JsonVariantConst result);

// GET /server/info: Klipper's state ("ready", "startup", ...) and whether the
// component "spoolman" is loaded. False when the answer has no klippy_state;
// the outputs are "" and false then.
bool moonrakerParseServerInfo(JsonVariantConst result, char* klippy_state, size_t n, bool* has_spoolman);

// GET /server/spoolman/status: spoolman_connected. False when the key is not
// a boolean; *connected is false then.
bool moonrakerParseSpoolmanStatus(JsonVariantConst result, bool* connected);

// GET /server/config: the address of Spoolman as Moonraker has it, as it is
// written (no scheme added, no slash cut): config.spoolman.server, else
// orig.spoolman.server. False when there is none or when it does not fit n
// (an address cut short could be another server's); out is "" then.
bool moonrakerParseSpoolmanServer(JsonVariantConst result, char* out, size_t n);

// The newest spool of a Spoolman: `spools` is the list asked with limit=1 and
// sorted by number, descending, so the first is the newest. An empty list is
// an answer: id 0 and registered "". False when it is no list, the first
// entry has no positive id, or its time does not fit n; *id is -1 then (the
// "could not be read" of klipperSameNewest). A missing time is "".
bool moonrakerParseNewestSpool(JsonVariantConst spools, int* id, char* registered, size_t n);
