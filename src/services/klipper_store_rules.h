#pragma once

#include <stdint.h>
#include <stddef.h>

// ============================================================
//  A KLIPPER PRINTER AS IT IS STORED: THE RULES
//
//  V46/V48 (Spec 5.3 Z, "docs/entwurf/2026-10-29-z-klipper.md"): the Klipper
//  printers the scale knows live in the settings memory (NVS), one record
//  each, and their API keys together in one list of their own. This unit
//  holds everything about that stored form that is not the memory itself:
//
//    record    one printer as one line of text and back, with the outcome of
//              its last connection test, so that the printer list can show
//              it without asking the printer again.
//    name      what a typed name is cleaned to, and when it is taken. The
//              name is also the storage location of the spools in Spoolman
//              (the printer's place), so two printers with one name would
//              be one place: the second is refused.
//    address   the host and the key the way they may be written.
//    keys      the list "<id>=<key>;<id>=<key>".
//    room      how many more printers the settings memory can take.
//    next id   a number that was used once is never used again.
//
//  The rules live here, without Arduino, ArduinoJson or LVGL: a host test
//  holds them to the values in test_klipper_store_rules.py, and the store
//  and the settings page ask this unit instead of keeping rules of their
//  own.
// ============================================================

#define KL_PRINTERS_MAX    64     // Klipper printers that can be stored
#define KL_ID_BASE         100000 // a Klipper printer's id is this or more (the first given out is one above)
#define KL_NAME_MAX        24     // name with the terminator: 23 bytes
#define KL_HOST_MAX        64     // host with the terminator: 63 bytes
#define KL_KEY_MAX         48     // API key with the terminator: 47 bytes
#define KL_RECORD_MAX      192    // one record as text, with the terminator
#define KL_KEYS_MAX        3200   // the whole list of keys, with the terminator
#define KL_NVS_PER_PRINTER 8      // entries of the settings memory one printer is counted at
#define KL_NVS_RESERVE     64     // entries that stay free for everything else
#define KL_PORT_DEFAULT    7125   // Moonraker's port

// What the connection test found out. Fixed numbers: they are stored.
enum KlipperTest : uint8_t {
  KT_NONE = 0,           // never tested
  KT_OK,
  KT_UNREACHABLE,        // Moonraker does not answer
  KT_ACCESS,             // answers, but the scale is not trusted (401/403)
  KT_KLIPPY,             // Klipper is not ready
  KT_NO_SPOOLMAN,        // no [spoolman] section in Moonraker
  KT_SM_OFFLINE,         // [spoolman] there, but Moonraker is not connected
  KT_OTHER_SPOOLMAN,     // the printer talks to another Spoolman than the scale
  KT_BAD_ANSWER          // an answer that is none (no JSON, wrong shape)
};

// Bits of KlipperTestOutcome::flags.
#define KL_FLAG_TOOLS_WITHOUT_VAR 0x01   // tool macros were found, none has a spool_id variable
#define KL_FLAG_TRUNCATED         0x02   // the printer has more fields than the scale shows

// The last connection test of one printer, as it is stored with it.
struct KlipperTestOutcome {
  uint8_t  test;     // KlipperTest
  uint8_t  layout;   // KlipperLayout (klipper_rules.h)
  uint8_t  fields;   // number of fields the printer showed
  uint8_t  flags;    // KL_FLAG_*
  uint32_t at;       // Unix seconds, 0 when unknown
};

struct KlipperPrinterCfg {
  uint32_t id;               // KL_ID_BASE or more, never used twice
  char     name[KL_NAME_MAX];
  char     host[KL_HOST_MAX];
  uint16_t port;
  KlipperTestOutcome last;
};

// The record of a printer as one line of text, version 1:
//   "1\t<id>\t<name>\t<host>\t<port>\t<test>\t<layout>\t<fields>\t<flags>\t<at>"
// The version comes first, so that a later layout can be told from this one
// before anything else is read; the outcome of the test comes last. A field
// added later goes on the end and is a new version. False when out has no
// room for the text and its terminator (out is then ""), and for a printer
// that decode would refuse: an id below KL_ID_BASE, port 0, an empty name or
// host, or a name or host with a control character (a tab would split the
// record).
bool klipperRecordEncode(const KlipperPrinterCfg& c, char* out, size_t n);

// The printer of a record. False for another version than 1, a missing or
// surplus field, a field that is not a plain number or does not fit its
// type, an id below KL_ID_BASE, port 0 or past 65535, and an empty name or
// host or one that does not fit its array. *out is written on success only.
bool klipperRecordDecode(const char* rec, KlipperPrinterCfg* out);

// A typed name as it is kept: control characters and the tab dropped, blanks
// at both ends gone, cut to KL_NAME_MAX - 1 bytes (or to what out holds, if
// less) and never in the middle of a UTF-8 character. False when nothing is
// left; out is then "".
bool klipperNameClean(const char* in, char* out, size_t n);

// True when the name is already one of names[0..count): equal after the
// accents are folded (accent_fold.h) and without regard to case, so that
// "voron 2.4" is "Voron 2.4" and an accent does not make another name.
// skip is the index of the printer that is being renamed, which does not
// count against itself, or -1. An empty name is never taken, nor is a null
// entry of the list.
bool klipperNameTaken(const char* name, const char* const* names, int count, int skip);

// A typed address as it is kept: blanks at both ends and a leading "http://"
// dropped, slashes at the end dropped. False for "https://" (the scale talks
// plain http to Moonraker), a blank, control character or "/" in the rest,
// nothing at all, and a host that does not fit KL_HOST_MAX (or n). The port
// is a field of its own, so none is read here.
bool klipperHostClean(const char* in, char* out, size_t n);

// An API key as it may be written: A-Z a-z 0-9 _ - only, at most KL_KEY_MAX
// - 1 characters. Nothing is trimmed, so a key pasted with a line break is
// refused instead of being sent in the header as it is. An empty key is
// allowed: it is how a key is taken away.
bool klipperKeyClean(const char* in, char* out, size_t n);

// The key of printer `id` from the list "<id>=<key>;<id>=<key>". False when
// the printer has none or the key does not fit n. The id is matched as a
// whole number. A null list is an empty one.
bool klipperKeysGet(const char* keys, uint32_t id, char* out, size_t n);

// The list with the key of printer `id` set, changed or (key "") taken away;
// a changed key keeps its place. False when the result does not fit n, or
// when the key is not one klipperKeyClean accepts; out is then "" and the
// caller keeps the list it has. out must not be the same memory as keys.
bool klipperKeysPut(const char* keys, uint32_t id, const char* key, char* out, size_t n);

// How many more printers can be added: what is left of KL_PRINTERS_MAX, but
// no more than the free entries of the settings memory allow after the
// reserve, each printer counted at KL_NVS_PER_PRINTER. Never negative.
int klipperRoomLeft(int stored, uint32_t available_entries);

// The id of the next printer: the larger of the counter that was stored, one
// more than the largest id in use, and KL_ID_BASE + 1. The counter is what
// keeps an id that was deleted from being given out again.
uint32_t klipperNextId(uint32_t stored_next, const uint32_t* ids, int count);
