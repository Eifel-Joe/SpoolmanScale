#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
//  THE FIRMWARE VERSION BAMBUDDY IS TOLD
//
//  BamBuddy takes a firmware_version of at most 20 characters, in the
//  registration and in every heartbeat (maziggy/bambuddy, branch dev,
//  backend/app/schemas/spoolbuddy.py: DeviceRegisterRequest and
//  HeartbeatRequest). A longer one is answered with HTTP 422 string_too_long,
//  so the scale was never registered and its heartbeat failed: BamBuddy listed
//  it as disconnected and could send it no command (tare, restart, write tag).
//
//  That is what the acceptance firmware ran into on the real scale: its
//  version is the release's plus the build's suffix, "v0.8.6-beta.14-abnahme",
//  22 characters. The suffix names the build to whoever looks at the scale
//  (status.json, log, web interface, header); BamBuddy does not need it and
//  the acceptance firmware has to register like the release it stands for.
//  Shortening the suffix was not chosen: the next longer version number
//  would break it again.
//
//  The rule lives here without Arduino so that test_bambuddy_device_rules.py
//  always holds it; bambuddy_device.cpp calls it.
// ============================================================

#define BB_FIRMWARE_VERSION_MAX  20   // the schema's limit, in characters

// Writes the version BamBuddy takes for the build's version into out (n bytes,
// always ended inside them):
//  - at most BB_FIRMWARE_VERSION_MAX characters: full as it is, suffix and all;
//  - longer: full without suffix, the release version, when it ends with
//    suffix and something is left without it; a suffix that is null, empty,
//    not at the end or all of full is no suffix to take away;
//  - still longer: its first BB_FIRMWARE_VERSION_MAX characters (versions are
//    ASCII, so no character is cut in half);
//  - then, if out is too small, as much of that as fits.
// True when what was written is not full, so that the caller can say once that
// BamBuddy is told something else. False for a null full (out then holds
// an empty text) and for no out or no room at all (n 0, nothing written).
bool bbFirmwareVersion(const char* full, const char* suffix, char* out, size_t n);

// ============================================================
//  THE SPACING OF THE REPORTS
//
//  Heartbeat and live weight go out from a task of their own (Spec 5.3 AC
//  a03, v0.8.7-beta.4, V71; AreYouThereAndi2, 7abb09a): on the loop task, a
//  BamBuddy that does not answer held touch and screen for 4 to 5 s per
//  report, a dozen times in six minutes in Andi's field log. A report is a
//  courtesy to the web interface, so it gets a short timeout, and is spaced
//  out by what the last one cost: a server that answers in 150 ms hears of
//  the weight once a second, one that does not answer at most every half
//  minute. Rules here, without Arduino, for test_bambuddy_report_rules.py.
// ============================================================

// BamBuddy marks a device offline after 30 seconds without a heartbeat
// (OFFLINE_THRESHOLD_SECONDS in its spoolbuddy routes), so this has to be
// well inside that. Its own daemon uses 10 s.
#define BB_HEARTBEAT_MS          10000
// Live weight for the web interface: at most one a second.
#define BB_WEIGHT_MIN_GAP_MS      1000
#define BB_HEARTBEAT_TIMEOUT_MS   3000
#define BB_WEIGHT_TIMEOUT_MS      1500
#define BB_BACKOFF_MAX_MS        30000
// A report may take a tenth of the time between two.
#define BB_REPORT_COST_FACTOR       10

// The gap to the next heartbeat after one that answered code: the plain rate
// for any answer (a 404 is one: the device was removed and registers again),
// twice the last gap for none, at most BB_BACKOFF_MAX_MS.
uint32_t bbHeartbeatGapMs(int code, uint32_t prev_gap_ms);

// The gap to the next weight report after one that answered code and took
// cost_ms: ten times the cost after an answer, twice the last gap after a
// failure; never closer than BB_WEIGHT_MIN_GAP_MS, never further than
// BB_BACKOFF_MAX_MS.
uint32_t bbWeightGapMs(int code, uint32_t cost_ms, uint32_t prev_gap_ms);
