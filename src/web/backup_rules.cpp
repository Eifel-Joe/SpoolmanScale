#include "web/backup_rules.h"

#include <string.h>

// The two groups that are not a plain setting, spelled out and never derived
// from a prefix: each name is a decision somebody made (see the header). A key
// in neither list is a setting.
static const char* const SECRET_KEYS[] = {
  "wifi_ssid", "wifi_pass", "filaman_key", "filaman_dev", "bb_key", "sm_secret", "sm_bind",
};
static const char* const DEVICE_KEYS[] = {
  "fw_ver", "fw_since", "upd_sha", "upd_ver",
  "rst_ok", "rst_recov", "rst_never", "rst_boots", "rst_due_at",
  "log_clr_seq", "web_master", "web_config", "web_maint", "web_pass",
};

static bool inList(const char* key, const char* const* list, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(key, list[i]) == 0) return true;
  }
  return false;
}

BackupKeyKind backupKeyKind(const char* key) {
  if (!key) return BACKUP_DEVICE;
  if (inList(key, SECRET_KEYS, sizeof(SECRET_KEYS) / sizeof(SECRET_KEYS[0]))) return BACKUP_SECRET;
  if (inList(key, DEVICE_KEYS, sizeof(DEVICE_KEYS) / sizeof(DEVICE_KEYS[0]))) return BACKUP_DEVICE;
  return BACKUP_SETTING;
}

bool backupKeyTravels(const char* key, bool with_secrets) {
  switch (backupKeyKind(key)) {
    case BACKUP_SETTING: return true;
    case BACKUP_SECRET:  return with_secrets;
    case BACKUP_DEVICE:  return false;
  }
  return false;
}

bool backupKeyValid(const char* key) {
  if (!key) return false;
  const size_t len = strlen(key);
  return len >= 1 && len <= (size_t)BACKUP_KEY_MAX;
}

bool backupHexEncode(const uint8_t* data, size_t n, char* out, size_t out_size) {
  // n > (out_size - 1) / 2 is 2 * n + 1 > out_size without the overflow a
  // huge n would give the multiplication.
  if (!out || out_size == 0 || n > (out_size - 1) / 2) return false;
  if (!data && n > 0) return false;
  static const char DIGITS[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[2 * i]     = DIGITS[data[i] >> 4];
    out[2 * i + 1] = DIGITS[data[i] & 0x0f];
  }
  out[2 * n] = '\0';
  return true;
}

// Value of one hex digit of either case, -1 for anything else: any byte
// outside the three ranges, a byte of a UTF-8 text included.
static int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool backupHexDecode(const char* hex, uint8_t* out, size_t out_size, size_t* out_n) {
  if (!hex || !out || !out_n) return false;
  const size_t len = strlen(hex);
  if (len < 2 || (len & 1) != 0) return false;
  const size_t n = len / 2;
  if (n > out_size) return false;
  // Every digit is read before the first byte is written, so a text that
  // goes wrong halfway leaves out as it was.
  for (size_t i = 0; i < len; i++) {
    if (hexDigit(hex[i]) < 0) return false;
  }
  for (size_t i = 0; i < n; i++) {
    out[i] = (uint8_t)((hexDigit(hex[2 * i]) << 4) | hexDigit(hex[2 * i + 1]));
  }
  *out_n = n;
  return true;
}
