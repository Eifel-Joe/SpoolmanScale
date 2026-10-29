#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// ============================================================
//  A KLIPPER PRINTER IN THE AMS VIEW: THE RULES
//
//  V46/V48 (Spec 5.3 Z, "docs/entwurf/2026-10-29-z-klipper.md"): a Klipper
//  printer has no AMS, but the AMS view draws it as one. What the printer
//  reports is cut into FIELDS (a gate of Happy Hare, a lane of AFC, a tool
//  macro that carries a spool_id, or the one active spool), the fields into
//  UNITS of at most four that the view draws as bays, and the assignment of
//  a spool to a field is turned into the G-code that does it. After the
//  commands the printer is read again and this unit says whether the field
//  now holds the spool.
//
//    detect    Happy Hare ahead of AFC ahead of the tool macros that have
//              the variable, else one field for the active spool.
//    units     at most four fields in a unit, at most four units.
//    commands  only the lines the printer is allowed to receive (see
//              klipperPlanAssign).
//    read-back the commands answer "ok" for a macro that does not exist, so
//              "ok" proves nothing; klipperFieldHolds is the proof.
//
//  The rules live here, without Arduino, ArduinoJson or LVGL: a host test
//  holds them to the values in test_klipper_rules.py. The parser of
//  Moonraker's answers (moonraker_parse) fills KlipperRaw, the glue to the
//  backend (klipper_printers) hands KlipperFacts to the view.
// ============================================================

#define KL_FIELDS_MAX  16    // fields of one printer; more are cut and noted
#define KL_RAW_MAX     24    // slots the parser keeps per kind of system
#define KL_LANE_MAX    24    // lane, unit and label texts, with the terminator
#define KL_MAT_MAX     12    // material text, with the terminator
#define KL_COLOR_MAX   10    // colour text without '#' (RRGGBBAA), with the terminator
// One script: the clearing of up to 15 other tools that hold the same spool
// is about 1.6 kB (R8). A KlipperPlan is about 4 kB: keep it off the stack.
#define KL_SCRIPT_MAX  2048
#define KL_STATE_MAX   16    // print_stats.state, with the terminator

// Errors of the glue, negative so that they sit beside HTTP codes.
#define KL_E_PARSE           (-2)
#define KL_E_NOT_HELD        (-40)   // the read-back did not find the spool
#define KL_E_OTHER_SPOOLMAN  (-41)   // the printer talks to another Spoolman
#define KL_E_NO_FIELD        (-42)
#define KL_E_NO_PRINTER      (-43)

// The layout is stored with the printer's last test: the numbers are fixed.
enum KlipperLayout : uint8_t {
  KL_LAYOUT_NONE = 0,
  KL_LAYOUT_SINGLE = 1,    // one field, the active spool
  KL_LAYOUT_TOOLS = 2,     // tool macros with a spool_id variable
  KL_LAYOUT_MMU = 3,       // Happy Hare
  KL_LAYOUT_AFC = 4        // Armored Turtle AFC
};

// Happy Hare's mmu.spoolman_support. Fixed numbers, they are stored.
enum KlipperMmuMode : uint8_t {
  KL_MMU_OFF = 0,
  KL_MMU_READONLY,
  KL_MMU_PUSH,
  KL_MMU_PULL,
  KL_MMU_UNKNOWN
};

// The text of mmu.spoolman_support, without regard to case; anything else
// (and a null) is KL_MMU_UNKNOWN.
KlipperMmuMode klipperMmuMode(const char* spoolman_support);

// One gate, lane or tool as the parser read it. spool_id 0 is none.
//   number    Happy Hare: the gate; AFC: the lane's index; tools: the n of T<n>
//   loaded    Happy Hare and AFC only; the tools read the printer's one
//             active tool instead (KlipperRaw::active_tool)
//   has_var   tools only: the macro has a spool_id variable
//   label     AFC: the lane's name as the commands spell it; tools: "T<n>"
//   unit      AFC: the name of the unit the lane sits in
struct KlipperRawSlot {
  int spool_id;
  int16_t number;
  bool loaded;
  bool has_var;
  char label[KL_LANE_MAX];
  char unit[KL_LANE_MAX];
  char material[KL_MAT_MAX];
  char color[KL_COLOR_MAX];
};

// Everything the parser found, one block per kind of system. The *_count is
// the full number, also past KL_RAW_MAX: only the first KL_RAW_MAX slots are
// filled. active_tool is -1 for none, progress -1 for unknown.
struct KlipperRaw {
  bool has_mmu;
  KlipperMmuMode mmu_mode;
  uint8_t mmu_count;
  KlipperRawSlot mmu[KL_RAW_MAX];
  bool has_afc;
  uint8_t afc_count;
  KlipperRawSlot afc[KL_RAW_MAX];
  uint8_t tool_count;              // tool macros, with or without the variable
  KlipperRawSlot tool[KL_RAW_MAX];
  int8_t active_tool;
  bool has_save_variables;
  int active_spool;
  char state[KL_STATE_MAX];        // print_stats.state as it came
  int8_t progress;                 // percent
  char unknown_system[KL_LANE_MAX];  // a system we cannot drive (a closed box)
};

struct KlipperField {
  int spool_id;
  int16_t number;
  bool loaded;
  char label[KL_LANE_MAX];
  char unit[KL_LANE_MAX];
  char material[KL_MAT_MAX];
  char color[KL_COLOR_MAX];
};

// What the view and the assignment work on.
//   tools_without_var  tool macros exist but none has the variable, so the
//                      printer is a single field and the screen says why
//   truncated          the printer has more than KL_FIELDS_MAX fields
struct KlipperFacts {
  KlipperLayout layout;
  KlipperMmuMode mmu_mode;          // KL_MMU_OFF unless the layout is KL_LAYOUT_MMU
  bool has_save_variables;
  bool tools_without_var;
  bool truncated;
  uint8_t field_count;
  KlipperField field[KL_FIELDS_MAX];
  int active_spool;
  int8_t active_tool;
  char state[KL_STATE_MAX];
  int8_t progress;
  char unknown_system[KL_LANE_MAX];
};

// Cuts what the printer reported into fields. `out` is written whole.
//   MMU     the gates, the loaded one is the gate in the extruder
//   AFC     the lanes, loaded is the lane's own flag
//   tools   only the macros with the variable, ordered by their number; the
//           loaded one is the printer's active tool
//   single  one field with the active spool, always loaded
// A system that reports no slot at all (a Happy Hare with no gate) is passed
// over for the next one.
void klipperBuild(const KlipperRaw& raw, KlipperFacts* out);

// The units the view draws: runs of at most four fields, in field order,
// without a gap, and at most four units (AMS_MAX_UNITS).
//   Happy Hare  "MMU <first>-<last>", the gate numbers as the printer has
//               them (from 0); a unit of one gate is "MMU <n>"
//   tools       "T<first>-T<last>", one tool "T<n>"
//   AFC         the name of the unit the lanes sit in ("AFC" when it has none);
//               more than four lanes of one name are split in fours, with the
//               same label
//   single      "" (the view puts its own caption)
// R7: where the AFC unit names would make more than four units, the names
// are dropped: the fields go into groups of four in field order, labelled "AFC".
uint8_t klipperUnitCount(const KlipperFacts& f);

struct KlipperUnit {
  uint8_t first;     // index of the unit's first field
  uint8_t count;     // fields in the unit, 1 to 4
  char label[KL_LANE_MAX];
};

// The unit `unit` (counted from 0); false when there is none. `out` is
// written whole, also on false.
bool klipperUnit(const KlipperFacts& f, uint8_t unit, KlipperUnit* out);

// A field's bay: ams_id is the unit, tray_id the place in it. False for a
// field that is not there; the values are left as they were.
bool klipperFieldBay(const KlipperFacts& f, uint8_t field, uint8_t* ams_id, uint8_t* tray_id);

// The inverse: the field of a bay, or -1 when there is none.
int klipperBayField(const KlipperFacts& f, uint8_t ams_id, uint8_t tray_id);

// The G-code that puts a spool into a field: a line per command, joined by
// '\n', without a trailing one.
//   clear         first: takes the spool out of the other fields that hold it
//                 (a spool sits in one place); empty where the printer does
//                 that itself
//   set           then: puts the spool into the field
//   set_active    the field is the active tool, so the printer's active spool
//                 (POST /server/spoolman/spool_id) has to follow
//   active_spool  the spool to send with it
struct KlipperPlan {
  char clear[KL_SCRIPT_MAX];
  char set[KL_SCRIPT_MAX];
  bool set_active;
  int active_spool;
};

// Happy Hare: OFF, READONLY, PUSH and UNKNOWN get MMU_GATE_MAP, PULL gets
// MMU_SPOOLMAN (the gate is taken from Spoolman). AFC: SET_SPOOL_ID with the
// lane. Tools: SET_GCODE_VARIABLE, and SAVE_VARIABLE where save_variables
// is there. The one field: no G-code, only the active spool.
// False for a field that is not there, a spool_id of 0 or less, an AFC lane
// without a name, or a script that does not fit KL_SCRIPT_MAX (R8: never
// half of one). `out` is written whole.
bool klipperPlanAssign(const KlipperFacts& f, uint8_t field, int spool_id, KlipperPlan* out);

// Whether `after`, the facts read back once the commands ran, hold the spool
// in the field. The one field is read from the active spool, the others from
// the field itself. False for a field that is not there.
// Root: Moonraker answers "ok" for a command of a macro that does not exist,
//   and Happy Hare's "pull" compares with Spoolman only after the command.
// Fix: the glue reads the printer again and asks this rule; only a yes is
//   "assigned".
// NOT-TO-DO: taking the answer of the command as proof. It is "ok" for a
//   misspelled lane as well.
// See test_klipper_rules.py "what the read-back must find".
bool klipperFieldHolds(const KlipperFacts& after, uint8_t field, int spool_id);

// Whether the location of a spool is the scale's to write: the setting is on
// and Happy Hare is not doing it (it writes the location itself in push and
// pull, a second writer would fight with it).
bool klipperWritesLocation(const KlipperFacts& f, bool switch_on);

// print_stats.state in the words printerActivity() of the AMS view knows:
// printing, paused, complete, standby, cancelled and error; any other text
// comes back as it is (the same pointer), a null as "".
const char* klipperStateWord(const char* print_stats_state);
