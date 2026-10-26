#pragma once

#include <ArduinoJson.h>
#include <stdint.h>

#include "app_config.h"

// ============================================================
//  THE VERSION FILE: WHICH RELEASE A BOARD TAKES
//
//  The release workflow writes version.json next to the web
//  flasher on our GitHub Pages (Spec 5.3 T): per board the
//  stable and the pre-release tag, each with the checksum and
//  the size of that board's image. The first board's keys are
//  the file's top level, every other board's sit in an object of
//  its own under "boards" (BOARD_VERSION_NODE).
//
//  Both checks read it: the daily one in the background
//  (update_check.cpp) and the one by hand (githubLatestTag() in
//  github_release.cpp, behind "Check" on the scale and on the web
//  page), and both take their release through versionFilePick(),
//  so they cannot pick differently.
//
//  Why the check by hand reads it too. Wurzel: it read GitHub's
//  release list, and that carries no checksum; an install right
//  after a check by hand, before the daily check had run, went in
//  "(no checksum published)" though one was published, and the
//  list, 55 kB as one String, came back cut at 32 KiB once.
//  Fix: the same file of a kilobyte, parsed off the stream. Its
//  release date is not in it; the web page has it from the notes
//  of the tag. NOT-TO-DO: GitHub's API for the same file, fresh at
//  once but 60 requests an hour per address; a query string or a
//  no-cache header to get past the pages' cache, which ignores
//  both (26.10.2026: max-age 600, the same age on every request).
//  It is not needed: the cache is cleared when the pages are
//  published, and v0.8.5-beta.9 was in the file a minute after
//  its release (published 20:30:14, served new 20:31:16 UTC).
// ============================================================

#define VERSION_FILE_URL FW_PAGES_URL "version.json"

// What a file says about this board.
enum VersionFileVerdict : uint8_t {
  VF_RELEASE = 0,     // a release for this board, in the pick
  VF_NONE_FOR_BOARD,  // this board's node names none yet: an answer, not a fault
  VF_BROKEN,          // the first board's top level names none: not our file
};

// One release out of the file. The strings point into the document and live
// as long as it does; neither is ever null.
struct VersionPick {
  const char* tag;    // "" without a release
  const char* sha;    // 64 hex digits, or "" from a file written before checksums
  uint32_t    size;   // the image's size, 0 when the file does not say
};

// The release of `doc` this board takes: with pre-releases on the
// pre-release, falling back to the stable one when the file carries none
// (which is the case right after a public release); with them off the stable
// one. `out` is filled in every case, an empty pick without a release.
VersionFileVerdict versionFilePick(JsonVariantConst doc, bool prerelease, VersionPick* out);
