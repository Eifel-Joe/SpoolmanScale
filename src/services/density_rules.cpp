#include "services/density_rules.h"

#include <string.h>

// ASCII letters only, as in spool_type_rules.cpp: a name with an umlaut is
// compared byte for byte, so no locale can fold half of a UTF-8 sequence.
static char foldAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

// Where the text of s begins and how many bytes it has without the blanks on
// both sides. Only the space counts as a blank, as in spool_type_rules.cpp.
static size_t trimmedSpan(const char* s, const char** begin) {
  size_t skip = 0;
  while (s[skip] == ' ') skip++;
  size_t end = strlen(s);
  while (end > skip && s[end - 1] == ' ') end--;
  *begin = s + skip;
  return end - skip;
}

// True when the first len bytes of a and b are the same, ASCII case aside.
static bool sameFolded(const char* a, const char* b, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (foldAscii(a[i]) != foldAscii(b[i])) return false;
  }
  return true;
}

bool densityPlausible(float d) {
  // Both comparisons are false for a value that is not a number.
  return d >= DENSITY_MIN_G_CM3 && d <= DENSITY_MAX_G_CM3;
}

bool densityEntryIsProduct(const char* entry_vendor, const char* entry_name, int entry_weight_g, const DensityProduct& want) {
  if (!entry_vendor || !entry_name || !want.vendor || !want.product) return false;

  // The vendor is the same, with the blanks at both ends and the case aside.
  const char* vendor;
  const size_t vlen = trimmedSpan(entry_vendor, &vendor);
  const char* want_vendor;
  const size_t wvlen = trimmedSpan(want.vendor, &want_vendor);
  if (vlen != wvlen || !sameFolded(vendor, want_vendor, vlen)) return false;

  // An empty product name names no product, so it matches nothing.
  const char* product;
  const size_t plen = trimmedSpan(want.product, &product);
  if (plen == 0) return false;

  // The name is the product name, or the product name, a blank and more text.
  // A longer word ("ABS-GF") is another product.
  const char* name;
  const size_t nlen = trimmedSpan(entry_name, &name);
  if (nlen < plen || !sameFolded(name, product, plen)) return false;
  if (nlen > plen && name[plen] != ' ') return false;

  // The net weight counts only where both name one.
  if (entry_weight_g > 0 && want.net_weight_g > 0 && entry_weight_g != want.net_weight_g) return false;
  return true;
}
