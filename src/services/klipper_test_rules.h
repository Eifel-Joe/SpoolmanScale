#pragma once

#include <stdint.h>
#include <stddef.h>

#include "services/klipper_store_rules.h"   // KlipperTest, the verdict this unit hands back

// ============================================================
//  THE CONNECTION TEST OF A KLIPPER PRINTER: THE RULES
//
//  V46/V48 (Spec 5.3 Z, "docs/entwurf/2026-10-29-z-klipper.md", 3.3): the
//  test only reads. Its answers are gathered in a KlipperProbe by the code
//  that talks to Moonraker, and this unit turns them into one verdict, in
//  the order of the specification, the first step that fails decides.
//
//  One question needs more than a status code: does the printer talk to the
//  same Spoolman as the scale? A spool number means the spool of ONE
//  Spoolman, so assigning number 87 at a printer on another Spoolman would
//  put somebody else's spool 87 into the field. The answer comes in two
//  steps:
//
//    address   Moonraker's Spoolman address against the scale's, written
//              alike: no schema, a slash, "/api", port 80 all mean the
//              same (klipperSameAddr).
//    newest    when the address differs ("spoolman" against "192.0.2.178")
//              the scale asks both servers for their newest spool and
//              compares number and time of registration
//              (klipperSameNewest). The same two answers mean one database.
//
//  The rules live here, without Arduino, ArduinoJson or LVGL: a host test
//  holds them to the values in test_klipper_test_rules.py.
// ============================================================

// The address of a Spoolman as it is compared: "<schema>://<host>:<port>" in
// lower case. A missing schema is http, a missing port 80 (http) or 443
// (https), and the path is dropped when it is "/" or ends in "/api" (a
// slash behind "/api" too). Any other path, another schema than http and
// https, an empty host, a character in the host other than a letter, digit,
// '.', '-' or '_' (so no user name and no IPv6 form), and a port that is not
// 1 to 65535 give false: such an address is never said to be the same as
// another. False also when the result does not fit n. out is "" then.
bool klipperSpoolmanAddr(const char* url, char* out, size_t n);

// True when both are the same address in the sense of klipperSpoolmanAddr.
// An address that is none is the same as nothing, not even as itself:
// "same" opens assigning, so it is never said on nothing.
bool klipperSameAddr(const char* a, const char* b);

// Whether two Spoolman servers hold the same database, judged by the newest
// spool of each. Fixed numbers: KS_UNKNOWN is the value a probe starts with.
enum KlipperSameDb : uint8_t { KS_UNKNOWN = 0, KS_SAME, KS_OTHER };

// The newest spool of each server: its number (0 when the server has no
// spool, -1 when it could not be read) and the time it was registered, as
// Spoolman writes it.
//   either number below 0           KS_UNKNOWN, nothing can be said
//   both 0                          KS_SAME: two empty databases cannot be
//                                   told apart, and with no spool on either
//                                   there is no number to be mixed up
//   one is 0                        KS_OTHER
//   number or time differs          KS_OTHER
//   otherwise                       KS_SAME
// A null time is an empty one.
KlipperSameDb klipperSameNewest(int a_id, const char* a_registered, int b_id, const char* b_registered);

// What the test found. Codes are HTTP status codes; 0 or less is no answer
// at all (nothing connected, time out). The fields behind a step that failed
// are not read and may hold anything.
struct KlipperProbe {
  int  access_code;        // status of the first request: any answer at all shows the printer is there
  int  info_code;          // status of the request that brings klippy_state: 200, 401/403 (not trusted), other
  char klippy_state[16];   // Klipper's state: "ready", "startup", "error", ... ("" when not read)
  bool has_spoolman;       // Moonraker has a [spoolman] section
  int  status_code;        // answer to the Spoolman status request
  bool sm_connected;       // Moonraker says it is connected to Spoolman
  bool addr_same;          // klipperSameAddr of Moonraker's address and the scale's
  KlipperSameDb newest;    // klipperSameNewest, KS_UNKNOWN when not asked
  // The Spoolman Moonraker talks to, for the words of KT_OTHER_SPOOLMAN: the
  // address as klipperSpoolmanAddr writes it, or as Moonraker has it where
  // the rules refuse it; "" when it was not read. Last, so that a probe
  // built by field keeps its meaning.
  char spoolman[KL_HOST_MAX + 16];
};

// The verdict, in the order of the specification; the first step that fails
// decides:
//   access_code <= 0                       KT_UNREACHABLE
//   info_code 401 or 403                   KT_ACCESS
//   info_code not 200                      KT_BAD_ANSWER
//   klippy_state not "ready"               KT_KLIPPY
//   no [spoolman]                          KT_NO_SPOOLMAN
//   status_code not 200 or not connected   KT_SM_OFFLINE
//   address differs and newest not SAME    KT_OTHER_SPOOLMAN
//   otherwise                              KT_OK
KlipperTest klipperTestVerdict(const KlipperProbe& p);
