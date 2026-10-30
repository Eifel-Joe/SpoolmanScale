#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "services/klipper_rules.h"
#include "services/klipper_store_rules.h"

// ============================================================
//  WEB JOBS
//
//  Several things a browser can ask for take seconds to answer: a test of the
//  backend address, the spool list for the tags page, the GitHub check, the
//  release notes and the connection test of a Klipper printer. Answered inside
//  the HTTP handler they held the loop task - display, scale, NFC, FilaMan's
//  device protocol - for up to eight seconds, because the handler runs on that
//  task.
//
//  Now the handler starts a job and answers 202. The job runs on its own
//  task, the same shape as the daily update check (core 0, below the loop
//  in priority, stack from the heap for as long as it runs), and the
//  browser asks the same URL again until the answer is ready. One job at a
//  time: a second TLS request beside the first would take another stack and
//  connection (~22-24 kB) from the internal heap, and the floors are not
//  meant for two.
//
//  What must not run on the second task stays out of it: LVGL, NVS and the
//  badges are touched by the handler that collects the result, on the loop
//  task; logSD() queues lines from other tasks; the stall counters only
//  count the loop task's own waits.
// ============================================================

enum WebJobKind : uint8_t {
  WJ_NONE = 0,
  WJ_HOST_TEST,   // GET the backend's health, 4 s
  WJ_SPOOLS,      // the spool list for the tags page, 8 s
  WJ_GH_CHECK,    // the latest tag on the chosen channel, TLS
  WJ_GH_NOTES,    // one release's notes, TLS
  WJ_BAMBU_CATALOG, // BambuStudio's colour table, TLS, converted and stored
  WJ_NET_PROBE,    // timing requests to the backend, see web_net_probe.h
  WJ_KLIPPER_TEST  // the read-only connection test of one Klipper printer, plain http
};

enum WebJobState : uint8_t {
  WJS_IDLE = 0,
  WJS_RUNNING,
  WJS_DONE
};

struct WebJobResult {
  WebJobKind kind;
  bool   ok;
  int    code;        // the HTTP code behind the answer, where there is one
  char   err[80];     // why not, when ok is false
  char   tag[40];     // GH check: the tag found
  char   sha[65];     // GH check: its checksum from version.json, "" without one
  char   pub[24];     // the colour table: how it was asked for
  uint32_t image_size; // GH check: the firmware image's size, 0 when unknown
  String body;        // a ready made reply body, for the jobs that have one
  // Klipper test: whose test it was and what it found. The job only fills
  // these in; the handler that collects the result writes the outcome to the
  // store, which belongs to the loop task. code is the HTTP code that decided
  // (KL_E_PARSE for a 200 that held no Klipper state).
  uint32_t kl_id;
  KlipperTestOutcome kl;
  char   kl_state[KL_STATE_MAX];   // Klipper's own state ("startup", "error", ...), "" when not read
  char   kl_unknown[KL_LANE_MAX];  // a system the scale cannot drive ("box"), "" when none
};

// Starts a job. `arg` is the release tag for WJ_GH_NOTES and the scale's
// Spoolman address for WJ_KLIPPER_TEST, unused otherwise; `flag` is the
// pre-release channel for WJ_GH_CHECK. False when a job is already running or
// the heap is too low for a second task.
bool webJobStart(WebJobKind kind, const char* arg, bool flag);

// The printer and its key for the next WJ_KLIPPER_TEST, copied here by the
// handler on the loop task before webJobStart() (R3). The job reads this copy
// and nothing of the store, which belongs to the loop task. False, and nothing
// copied, while any job is in flight: a running test reads the old copy.
bool webJobSetKlipperInput(const KlipperPrinterCfg& cfg, const char* key);

WebJobState webJobState();
WebJobKind  webJobKind();

// The result, valid while the state is WJS_DONE. webJobTake() hands the slot
// back; a handler calls it once it has sent the answer.
const WebJobResult& webJobResult();
void webJobTake();

// From appLoop(): drops a result no browser came back for.
void webJobsTick();
