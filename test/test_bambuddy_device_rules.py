"""Compile the rule for the firmware version the scale tells BamBuddy (services/bambuddy_device_rules.h) and check it: BamBuddy's schema refuses a firmware_version of more than 20 characters (HTTP 422), so the acceptance firmware, whose version is the release version plus "-abnahme" (22 characters), could neither register nor send its heartbeat. The rule sends the version as it is when it fits, otherwise the release version without the build's suffix, and if even that is too long its first 20 characters; the answer always ends inside the buffer it is given. The last block compiles the real FW_VERSION of an acceptance build (SSC_ACCEPTANCE, from app_config.h) through the rule."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/bambuddy_device_rules.h"
#include "app_config.h"
#include <cassert>
#include <cstring>
#include <string>

// The version the rule writes into a buffer of the given size. The buffer
// starts as garbage so that an answer the rule does not end shows, and the
// byte behind the buffer must stay as it was.
static std::string sent(const char* full, const char* suffix, size_t size, bool* cut = nullptr) {
  char buf[64];
  memset(buf, 0x5A, sizeof(buf));
  assert(size > 0 && size <= sizeof(buf));
  const bool c = bbFirmwareVersion(full, suffix, buf, size);
  if (cut) *cut = c;
  assert(memchr(buf, '\0', size) != nullptr);          // always ended inside the buffer
  assert(size == sizeof(buf) || buf[size] == 0x5A);    // nothing written behind it
  return std::string(buf);
}

int main() {
  static_assert(BB_FIRMWARE_VERSION_MAX == 20, "BamBuddy's schema: at most 20 characters");
  const size_t big = BB_FIRMWARE_VERSION_MAX + 1;      // what the scale itself gives the rule
  bool cut = true;

  // --- a version that fits is sent as it is ------------------------------------
  // The release: no suffix (an empty one, or none given).
  assert(sent("v0.8.6-beta.14", "", big, &cut) == "v0.8.6-beta.14" && !cut);
  assert(sent("v0.8.6-beta.14", nullptr, big, &cut) == "v0.8.6-beta.14" && !cut);
  // The documentation firmware, 19 characters, suffix and all.
  assert(strlen("v0.8.6-beta.14-doku") == 19);
  assert(sent("v0.8.6-beta.14-doku", "-doku", big, &cut) == "v0.8.6-beta.14-doku" && !cut);
  // Exactly 20 characters still fits, and its suffix stays.
  assert(strlen("v0.8.6-beta.14-abcde") == 20);
  assert(sent("v0.8.6-beta.14-abcde", "-abcde", big, &cut) == "v0.8.6-beta.14-abcde" && !cut);
  // A suffix longer than the version it is asked about cuts nothing.
  assert(sent("v1", "-abnahme", big, &cut) == "v1" && !cut);

  // --- too long: the release version without the suffix -------------------------
  // The acceptance firmware, 22 characters, which BamBuddy answered with 422.
  assert(strlen("v0.8.6-beta.14-abnahme") == 22);
  assert(sent("v0.8.6-beta.14-abnahme", "-abnahme", big, &cut) == "v0.8.6-beta.14" && cut);
  // 21 characters is one too many.
  assert(strlen("v0.8.6-beta.14-abcdef") == 21);
  assert(sent("v0.8.6-beta.14-abcdef", "-abcdef", big, &cut) == "v0.8.6-beta.14" && cut);

  // --- still too long without the suffix: the first 20 characters -----------------
  assert(strlen("v10.20.30-beta.1234567") == 22);
  assert(sent("v10.20.30-beta.1234567", "", big, &cut) == "v10.20.30-beta.12345" && cut);
  assert(sent("v10.20.30-beta.1234567", nullptr, big, &cut) == "v10.20.30-beta.12345" && cut);
  assert(sent("v10.20.30-beta.1234567-abnahme", "-abnahme", big, &cut) == "v10.20.30-beta.12345" && cut);
  // A suffix that is not at the end is not the build's suffix: nothing is taken out.
  assert(strlen("v0.8.6-beta.14-abnahme-x") == 24);
  assert(sent("v0.8.6-beta.14-abnahme-x", "-abnahme", big, &cut) == "v0.8.6-beta.14-abnah" && cut);
  // A version that is nothing but the suffix has no release part to fall back to.
  assert(strlen("-abcdefghijklmnopqrstu") == 22);
  assert(sent("-abcdefghijklmnopqrstu", "-abcdefghijklmnopqrstu", big, &cut) == "-abcdefghijklmnopqrs" && cut);

  // --- a small buffer: cut, and ended -------------------------------------------
  assert(sent("v0.8.6-beta.14", "", 15, &cut) == "v0.8.6-beta.14" && !cut);       // fits exactly
  assert(sent("v0.8.6-beta.14", "", 14, &cut) == "v0.8.6-beta.1" && cut);
  assert(sent("v0.8.6-beta.14", "", 8, &cut) == "v0.8.6-" && cut);
  assert(sent("v0.8.6-beta.14", "", 1, &cut) == "" && cut);
  assert(sent("v0.8.6-beta.14-abnahme", "-abnahme", 10, &cut) == "v0.8.6-be" && cut);
  assert(sent("v0.8.6-beta.14-abnahme", "-abnahme", 15, &cut) == "v0.8.6-beta.14" && cut);
  // No buffer, nothing written; no version, an empty answer.
  {
    char buf[8];
    memset(buf, 0x5A, sizeof(buf));
    assert(!bbFirmwareVersion("v0.8.6-beta.14", "", buf, 0));
    assert(buf[0] == 0x5A);
    assert(!bbFirmwareVersion("v0.8.6-beta.14", "", nullptr, sizeof(buf)));
    assert(!bbFirmwareVersion(nullptr, "", buf, sizeof(buf)) && buf[0] == '\0');
  }

  // --- whatever it is given, what comes out fits and starts the version -----------
  const char* const versions[] = {
    "", "v1", "v0.8.6-beta.14", "v0.8.6-beta.14-doku", "v0.8.6-beta.14-abcde",
    "v0.8.6-beta.14-abnahme", "v10.20.30-beta.1234567-abnahme", "0123456789012345678901234567890123456789",
  };
  const char* const suffixes[] = {nullptr, "", "-doku", "-abnahme", "e", "0123456789012345678901234567890123456789"};
  for (const char* v : versions) {
    for (const char* s : suffixes) {
      bool c = false;
      const std::string out = sent(v, s, big, &c);
      assert(out.size() <= (size_t)BB_FIRMWARE_VERSION_MAX);
      assert(strncmp(out.c_str(), v, out.size()) == 0);   // always the start of the version
      assert(c == (out.size() != strlen(v)));              // "cut" says whether it differs
    }
  }

  // --- the firmware as it is built ------------------------------------------------
  // This test is compiled with SSC_ACCEPTANCE, so FW_VERSION carries the suffix
  // the acceptance firmware has. Whatever the version is, BamBuddy must get
  // something it takes, and when the release version alone fits, that is what
  // it must at least be.
  static_assert(sizeof(SSC_VERSION_SUFFIX) > 1, "compiled with SSC_ACCEPTANCE: FW_VERSION has a suffix");
  {
    const size_t release_len = sizeof(FW_VERSION) - sizeof(SSC_VERSION_SUFFIX);
    char real[BB_FIRMWARE_VERSION_MAX + 1];
    bbFirmwareVersion(FW_VERSION, SSC_VERSION_SUFFIX, real, sizeof(real));
    assert(strlen(real) <= (size_t)BB_FIRMWARE_VERSION_MAX);
    assert(strncmp(real, FW_VERSION, strlen(real)) == 0);
    if (release_len <= (size_t)BB_FIRMWARE_VERSION_MAX) {
      assert(strlen(real) >= release_len);
    }
  }
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-DSSC_ACCEPTANCE", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "bambuddy_device_rules.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("test_bambuddy_device_rules: ok")
