#include "services/spool_extras_rules.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

bool extrasPlaceUsed(const SpoolExtra* list, int i) {
  // The range comes first: the list holds eight entries and nothing else.
  return list && i >= 0 && i < SPOOL_EXTRAS_MAX && list[i].name[0] != '\0';
}

uint8_t extrasClampMask(const SpoolExtra* list, uint8_t mask) {
  uint8_t out = 0;
  int picked = 0;
  for (int i = 0; i < SPOOL_EXTRAS_MAX && picked < SPOOL_EXTRAS_PICK; i++) {
    if ((mask & (1u << i)) && extrasPlaceUsed(list, i)) {
      out |= (uint8_t)(1u << i);
      picked++;
    }
  }
  return out;
}

float extrasMaskGrams(const SpoolExtra* list, uint8_t mask) {
  // Through the cut, so that an unused place never counts, whatever it holds.
  mask = extrasClampMask(list, mask);
  float g = 0.0f;
  for (int i = 0; i < SPOOL_EXTRAS_MAX; i++) {
    if (mask & (1u << i)) g += list[i].grams;
  }
  return g;
}

void extrasMaskNames(const SpoolExtra* list, uint8_t mask, char* out, size_t n) {
  if (!out || n == 0) return;
  out[0] = '\0';
  mask = extrasClampMask(list, mask);
  size_t used = 0;
  // used < n at every pass, so there is always room for the terminator that
  // snprintf writes. It answers how long the whole would have been, so a cut
  // name pushes used to n or beyond and ends the loop; nothing is written
  // at out[n] or later.
  for (int i = 0; i < SPOOL_EXTRAS_MAX && used < n; i++) {
    if (!(mask & (1u << i))) continue;
    const int w = snprintf(out + used, n - used, "%s%s", used ? " + " : "", list[i].name);
    if (w < 0) {
      out[used] = '\0';
      return;
    }
    used += (size_t)w;
  }
}

float extrasClampGrams(float g) {
  // <= and not <, so that a minus zero comes out as a plain zero.
  if (!isfinite(g) || g <= 0.0f) return 0.0f;
  return g > SPOOL_EXTRA_MAX_G ? SPOOL_EXTRA_MAX_G : g;
}

void extrasTrimName(char* dst, size_t n, const char* src) {
  if (!dst || n == 0) return;
  if (!src) src = "";
  while (*src == ' ') src++;
  size_t len = strlen(src);
  if (len > n - 1) len = n - 1;
  // memmove, so that trimming a name in place (src is dst) works too.
  memmove(dst, src, len);
  dst[len] = '\0';
  // After the cut, which may have ended on a blank.
  while (len && dst[len - 1] == ' ') dst[--len] = '\0';
}

uint8_t extrasResolve(const SpoolExtra* list, uint8_t stored, uint8_t default_mask) {
  if (stored == SPOOL_EXTRAS_FOLLOW_DEFAULT) return extrasClampMask(list, default_mask);
  return extrasClampMask(list, stored);
}

uint8_t extrasToStore(const SpoolExtra* list, uint8_t pick, uint8_t default_mask) {
  const uint8_t mask = extrasClampMask(list, pick);
  if (mask == extrasClampMask(list, default_mask)) return (uint8_t)SPOOL_EXTRAS_FOLLOW_DEFAULT;
  return mask;
}

float extrasBare(float reading, float parts_g) {
  const float g = reading - parts_g;
  return (isfinite(g) && g > 0.0f) ? g : 0.0f;
}
