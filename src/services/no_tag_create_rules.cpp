#include "services/no_tag_create_rules.h"

#include <string.h>

// ASCII letters only, on purpose: a vendor with an umlaut is compared byte
// for byte, so that no locale and no half-folded UTF-8 sequence can sneak in.
static char foldAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

// True when the text s of length len (no terminator needed) is the word w,
// ASCII case aside.
static bool isWordFolded(const char* s, size_t len, const char* w) {
  if (strlen(w) != len) return false;
  for (size_t i = 0; i < len; i++) {
    if (foldAscii(s[i]) != foldAscii(w[i])) return false;
  }
  return true;
}

bool noTagModeAtEntry(bool bambu_tag, bool have_uid) {
  return !bambu_tag && !have_uid;
}

bool noTagSourceOffered(bool no_tag, bool bambu_tag, bool have_uid) {
  return !bambu_tag && (no_tag || have_uid);
}

float noTagGross(bool no_tag, float bare) {
  return no_tag ? 0.0f : bare;
}

bool noTagTypeAsk(bool types_active, bool no_tag, float bare, int type_g) {
  if (!types_active || type_g > 0) return false;
  // The comparison is false for a reading that is not a number.
  return no_tag || bare > NEW_SPOOL_TARE_MIN_G;
}

bool noTagFillMissing(bool no_tag, int label_g) {
  return no_tag && label_g <= 0;
}

bool noTagFillPlausible(float grams) {
  // Both comparisons are false for a value that is not a number.
  return grams >= (float)NO_TAG_FILL_MIN_G && grams <= (float)NO_TAG_FILL_MAX_G;
}

NoTagNote noTagCreatedNote(const char* vendor) {
  if (!vendor) return NTN_PLAIN;
  // The blanks on both sides do not count.
  const char* begin = vendor;
  while (*begin == ' ') begin++;
  size_t len = strlen(begin);
  while (len > 0 && begin[len - 1] == ' ') len--;
  if (isWordFolded(begin, len, BAMBU_VENDOR_NAME) || isWordFolded(begin, len, "Bambu")) {
    return NTN_BAMBU_CHIP;
  }
  return NTN_PLAIN;
}

bool amsInfoOffered(bool can_assign, bool has_ams, bool found, bool archived, int spool_id) {
  return can_assign && has_ams && found && !archived && spool_id > 0;
}
