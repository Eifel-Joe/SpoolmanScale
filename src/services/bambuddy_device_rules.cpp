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

static uint32_t doubledUpToMax(uint32_t gap_ms) {
  return gap_ms < BB_BACKOFF_MAX_MS / 2 ? gap_ms * 2 : BB_BACKOFF_MAX_MS;
}

uint32_t bbHeartbeatGapMs(int code, uint32_t prev_gap_ms) {
  if (code == 200 || code == 404) return BB_HEARTBEAT_MS;
  return doubledUpToMax(prev_gap_ms < BB_HEARTBEAT_MS ? BB_HEARTBEAT_MS : prev_gap_ms);
}

uint32_t bbWeightGapMs(int code, uint32_t cost_ms, uint32_t prev_gap_ms) {
  uint32_t gap = (code == 200) ? cost_ms * BB_REPORT_COST_FACTOR : doubledUpToMax(prev_gap_ms);
  if (gap < BB_WEIGHT_MIN_GAP_MS) gap = BB_WEIGHT_MIN_GAP_MS;
  if (gap > BB_BACKOFF_MAX_MS) gap = BB_BACKOFF_MAX_MS;
  return gap;
}
