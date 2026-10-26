#include "services/version_file.h"

#include <string.h>

#include "boards/board.h"

VersionFileVerdict versionFilePick(JsonVariantConst doc, bool prerelease, VersionPick* out) {
  VersionPick pick = {"", "", 0};
  // Any other board than the first reads its own node and never the top
  // level: that tag may have no image this board could run.
  const bool own_node = (BOARD_VERSION_NODE[0] != '\0');
  JsonVariantConst node = own_node ? doc["boards"][BOARD_VERSION_NODE] : doc;

  const char* tag = nullptr;
  const char* sha = nullptr;
  uint32_t size = 0;
  if (prerelease) {
    tag  = node["prerelease"].as<const char*>();
    sha  = node["prerelease_sha256"].as<const char*>();
    size = node["prerelease_size"] | 0u;
  }
  if (!tag || tag[0] == '\0') {
    tag  = node["stable"].as<const char*>();
    sha  = node["stable_sha256"].as<const char*>();
    size = node["stable_size"] | 0u;
  }
  // The size goes along even without a tag, as the daily check always kept it.
  pick.size = size;
  if (!tag || tag[0] == '\0') {
    if (out) *out = pick;
    return own_node ? VF_NONE_FOR_BOARD : VF_BROKEN;
  }
  pick.tag = tag;
  if (sha && strlen(sha) == 64) pick.sha = sha;
  if (out) *out = pick;
  return VF_RELEASE;
}
