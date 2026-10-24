"""Compile the production settings backup rules and check what travels, what never does, and the hex round trip."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "web/backup_rules.h"
#include <cassert>
#include <cstring>

// The three groups, spelled out here on purpose: the production lists must
// agree with this text, not with themselves.
static const char* const SECRETS[] = {
  "wifi_ssid", "wifi_pass", "filaman_key", "filaman_dev", "bb_key", "sm_secret"
};
static const char* const DEVICE[] = {
  "fw_ver", "fw_since", "upd_sha", "upd_ver",
  "rst_ok", "rst_recov", "rst_never", "rst_boots", "rst_due_at",
  "log_clr_seq", "web_master", "web_config", "web_maint", "web_pass"
};
static const char* const SETTINGS[] = {
  "cal_factor", "zero_offset", "mdns_host", "bb_host", "sm_user", "lang",
  "tag_mv27", "bright"
};

// A key is at most BACKUP_KEY_MAX characters: 15 fit, 16 do not.
static_assert(sizeof("abcdefghijklmno") - 1 == (size_t)BACKUP_KEY_MAX, "15 characters");
static_assert(sizeof("abcdefghijklmnop") - 1 == (size_t)BACKUP_KEY_MAX + 1, "16 characters");

int main() {
  // Which group a key belongs to. Every listed key is also a legal NVS key.
  for (const char* k : SECRETS) {
    assert(backupKeyKind(k) == BACKUP_SECRET);
    assert(backupKeyValid(k));
  }
  for (const char* k : DEVICE) {
    assert(backupKeyKind(k) == BACKUP_DEVICE);
    assert(backupKeyValid(k));
  }
  // Anything not named above is a setting, the calibration included.
  for (const char* k : SETTINGS) {
    assert(backupKeyKind(k) == BACKUP_SETTING);
    assert(backupKeyValid(k));
  }
  assert(backupKeyKind("unknown_key") == BACKUP_SETTING);

  // A setting travels either way, a secret only when the file may carry
  // secrets, a device fact never.
  for (const char* k : SETTINGS) {
    assert(backupKeyTravels(k, false));
    assert(backupKeyTravels(k, true));
  }
  assert(!backupKeyTravels("bb_key", false));
  assert(backupKeyTravels("bb_key", true));
  for (const char* k : SECRETS) {
    assert(!backupKeyTravels(k, false));
    assert(backupKeyTravels(k, true));
  }
  assert(!backupKeyTravels("web_maint", false));
  assert(!backupKeyTravels("web_maint", true));
  assert(!backupKeyTravels("log_clr_seq", false));
  assert(!backupKeyTravels("log_clr_seq", true));
  for (const char* k : DEVICE) {
    assert(!backupKeyTravels(k, false));
    assert(!backupKeyTravels(k, true));
  }
  // No key at all is not something to carry.
  assert(!backupKeyTravels(nullptr, false));
  assert(!backupKeyTravels(nullptr, true));

  // NVS keys run from 1 to 15 characters.
  assert(!backupKeyValid(nullptr));
  assert(!backupKeyValid(""));
  assert(!backupKeyValid("abcdefghijklmnop"));
  assert(backupKeyValid("a"));
  assert(backupKeyValid("abcdefghijklmno"));

  // A float makes the trip as its four bytes, little endian, lowercase hex.
  float f = 1234.5f;
  uint8_t raw[sizeof(f)];
  memcpy(raw, &f, sizeof(raw));
  char hex[9];
  assert(backupHexEncode(raw, sizeof(raw), hex, sizeof(hex)));
  assert(strcmp(hex, "00509a44") == 0);
  uint8_t back[4];
  size_t n = 0;
  assert(backupHexDecode(hex, back, sizeof(back), &n));
  assert(n == 4 && memcmp(back, raw, sizeof(raw)) == 0);
  float g = 0.0f;
  memcpy(&g, back, sizeof(g));
  assert(g == 1234.5f);

  // Lowercase out, either case in.
  const uint8_t abc[] = { 0xab, 0xcd, 0xef };
  char six[7];
  assert(backupHexEncode(abc, sizeof(abc), six, sizeof(six)));
  assert(strcmp(six, "abcdef") == 0);
  uint8_t two[2];
  assert(backupHexDecode("0A0b", two, sizeof(two), &n));
  assert(n == 2 && two[0] == 0x0a && two[1] == 0x0b);

  // Fits exactly: two bytes into two.
  assert(backupHexDecode("0102", two, sizeof(two), &n) && n == 2);

  // What is no blob: odd length, a letter that is no digit, nothing at all
  // (a blob holds at least one byte), and three bytes for a buffer of two.
  assert(!backupHexDecode("abc", back, sizeof(back), &n));
  assert(!backupHexDecode("zz", back, sizeof(back), &n));
  assert(!backupHexDecode("0a0z", back, sizeof(back), &n));
  assert(!backupHexDecode("", back, sizeof(back), &n));
  assert(!backupHexDecode("010203", two, sizeof(two), &n));
  assert(!backupHexDecode(nullptr, back, sizeof(back), &n));

  // Encoding needs two characters per byte and the terminator: four bytes
  // do not fit in eight characters, which leaves no room for the NUL.
  char eight[8];
  assert(!backupHexEncode(raw, sizeof(raw), eight, sizeof(eight)));
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "web" / "backup_rules.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("settings backup rules: ok")
