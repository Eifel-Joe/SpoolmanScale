#pragma once

#include <stddef.h>
#include <stdint.h>

#include "services/klipper_rules.h"
#include "services/klipper_store_rules.h"
#include "services/klipper_test_rules.h"

// ============================================================
//  MOONRAKER REST CLIENT
//
//  V46/V48 (Spec 5.3 Z, "docs/entwurf/2026-10-29-z-klipper.md"): the one place
//  that talks to a Klipper printer. It reads the facts the AMS view draws
//  (moonraker_parse turns the answers into klipper_rules.h structs), sends the
//  commands the plan of klipper_rules names, sets the printer's active spool,
//  and runs the read-only connection test.
//
//  Plain http only, to the address and port the user stored; the key, if there
//  is one, goes in the header X-Api-Key and nowhere else (not in an address,
//  not in the log). Time limits: KL_CONNECT_MS to connect, KL_ANSWER_MS for the
//  answer. Every answer is logged with its code ("KLIPPER: POST http://... -> 200");
//  a GET that worked only at verbose level ("[verbose] KLIPPER: GET ... -> 200"),
//  because the AMS view reads a printer three times per refresh.
//
//  Every function returns the HTTP status code of the request that decided it,
//  with these that predate any request:
//     -1  the request could not be built or the memory for it is missing
//     -2  (KL_E_PARSE) an answer that is none: no JSON, or not the shape asked for
//  and the codes of the HTTP client below zero for a connection that failed.
//
//  THREADS: no state is kept between calls (the working memory is taken per
//  call), so the connection test may run on a task of its own beside the loop.
// ============================================================

#define KL_CONNECT_MS  3000    // connecting to Moonraker
#define KL_ANSWER_MS   8000    // the answer, once connected (a script runs to its end first)

// Working memory for the callers that need more than a stack allows: zeroed,
// from the PSRAM, from the internal RAM when there is none (or it is full).
// A KlipperRaw is about 5.8 kB and a KlipperPlan about 4 kB: neither belongs on
// a task's stack (R15, R8). Null when there is no memory at all.
void* moonrakerAlloc(size_t bytes);
void  moonrakerFree(void* block);

// Reads what the printer has and holds: the list of its objects, one query for
// the systems on it and the printer's active spool (the three requests of
// moonraker_parse.h), cut into fields by klipperBuild(). *out is zero (layout
// KL_LAYOUT_NONE, no fields) unless the code is 200.
// A printer without [spoolman] has no active spool to give (the request is
// refused with a code, not a transport failure): its facts are complete with
// an active_spool of 0. Only a connection that breaks stops the read.
int moonrakerFetchFacts(const KlipperPrinterCfg& c, const char* key, KlipperFacts* out);

// POST /printer/gcode/script with {"script": ...}: the lines are joined by '\n'.
// Moonraker answers 200 "ok" after the last line ran, and also for a macro that
// does not exist: the answer proves nothing about the effect (klipperFieldHolds
// is the proof). A line Klipper refuses answers 400. Null or empty: -1, nothing
// is sent.
int moonrakerRunScript(const KlipperPrinterCfg& c, const char* key, const char* script);

// POST /server/spoolman/spool_id with {"spool_id": n}. Only for n above 0: a
// request without a number would CLEAR the active spool (Moonraker takes the
// missing parameter for null), so nothing is sent for 0 or less (-1).
int moonrakerSetActiveSpool(const KlipperPrinterCfg& c, const char* key, int spool_id);

// The connection test, read-only (Spec 5.3 Z 3.3): fills *probe in the order of
// klipperTestVerdict(), the first step that fails ends the questions.
//   GET  /access/info, /server/info                    always
//   GET  /server/spoolman/status, /server/config       while Klipper is ready and [spoolman] is there
//   POST /server/spoolman/proxy (GET /v1/spool, the newest spool) beside
//   GET  <scale_spoolman>/api/v1/spool                 when the two addresses differ
// and, when Klipper is ready and `facts` is not null, the facts of
// moonrakerFetchFacts() (zero otherwise). The proxy request changes nothing:
// it carries a GET to Spoolman.
// Returns 200 when the printer answered at all (the verdict is
// klipperTestVerdict(*probe)), else the code that stopped the first request
// (zero or less: nothing connected).
// info_code is KL_E_PARSE when /server/info answered 200 with no Klipper state.
int moonrakerProbe(const KlipperPrinterCfg& c, const char* key, const char* scale_spoolman,
                   KlipperProbe* probe, KlipperFacts* facts);
