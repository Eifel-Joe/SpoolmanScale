#pragma once

#include <stddef.h>

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
