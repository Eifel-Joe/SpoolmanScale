#include "services/bambuddy_device_rules.h"

#include <string.h>

bool bbFirmwareVersion(const char* full, const char* suffix, char* out, size_t n) {
  if (!out || n == 0) return false;
  out[0] = '\0';
  if (!full) return false;

  const size_t full_len = strlen(full);
  size_t len = full_len;
  if (len > (size_t)BB_FIRMWARE_VERSION_MAX) {
    const size_t suffix_len = suffix ? strlen(suffix) : 0;
    // suffix_len < len: a version that is nothing but the suffix has no
    // release part to fall back to, the first characters are all there is.
    if (suffix_len > 0 && suffix_len < len && memcmp(full + len - suffix_len, suffix, suffix_len) == 0) {
      len -= suffix_len;
    }
    if (len > (size_t)BB_FIRMWARE_VERSION_MAX) len = BB_FIRMWARE_VERSION_MAX;
  }
  if (len > n - 1) len = n - 1;
  memcpy(out, full, len);
  out[len] = '\0';
  return len != full_len;
}
