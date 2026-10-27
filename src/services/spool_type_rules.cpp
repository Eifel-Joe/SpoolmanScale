#include "services/spool_type_rules.h"

#include <math.h>
#include <string.h>

// The most grams spoolTypeGrams() hands back. Far above any spool, and low
// enough for the float to int cast to be safe.
static const float kGramsCap = 1000000.0f;

// ASCII letters only, on purpose: a name with an umlaut is compared byte for
// byte, so that no locale and no half-folded UTF-8 sequence can sneak in.
static char foldAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

// Where the text of s begins and how long it is without the blanks on both
// sides. Looks at no more than max bytes and stops at the terminator.
static size_t spanTrimmed(const char* s, size_t max, const char** begin) {
  size_t skip = 0;
  while (skip < max && s[skip] == ' ') skip++;
  size_t end = skip;
  while (end < max && s[end] != '\0') end++;
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

// How far a type is from the weight. Recomputed where it is needed instead of
// kept in a table, so that out_max can be anything the caller likes.
static float typeDistance(const SpoolType* list, int i, float grams) {
  return fabsf((float)list[i].grams - grams);
}

bool spoolTypeOfVendor(const char* type_name, const char* vendor) {
  if (!type_name || !vendor) return false;
  const char* v;
  const size_t vlen = spanTrimmed(vendor, strlen(vendor), &v);
  if (vlen == 0) return false;
  const char* t;
  const size_t tlen = spanTrimmed(type_name, (size_t)SPOOL_TYPE_NAME_MAX, &t);
  if (tlen < vlen || !sameFolded(t, v, vlen)) return false;
  // The prefix has to end where a word ends. t[vlen] exists only when the
  // name goes on (tlen > vlen); the trailing blanks were cut off above.
  return tlen == vlen || t[vlen] == ' ' || t[vlen] == '-';
}

int spoolTypeFilter(const SpoolType* list, int n, const char* vendor, int* out, int out_max) {
  if (!list || !out || n <= 0 || out_max <= 0) return 0;
  int count = 0;
  for (int i = 0; i < n && count < out_max; i++) {
    if (spoolTypeOfVendor(list[i].name, vendor)) out[count++] = i;
  }
  return count;
}

int spoolTypeNear(const SpoolType* list, int n, const SpoolTypeNearQuery& q, int* out, int out_max) {
  const char* vendor = q.vendor;
  const float grams  = q.grams;
  const float tol    = q.tol;
  if (!list || !out || n <= 0 || out_max <= 0) return 0;
  // The negated comparisons also catch a value that is not a number.
  if (!isfinite(grams) || grams <= 0.0f) return 0;
  if (!(tol >= 0.0f)) return 0;

  // A vendor with no type in the list has nothing to narrow the list down to.
  bool vendor_has_types = false;
  for (int i = 0; i < n && !vendor_has_types; i++) {
    vendor_has_types = spoolTypeOfVendor(list[i].name, vendor);
  }

  // out is kept sorted by distance as the list is walked, and holds the
  // nearest out_max types seen so far. A type goes behind the ones at the same
  // distance (they came earlier in the list), and the one that falls off the
  // end is the farthest. Nothing is written at out[out_max] or later.
  int count = 0;
  for (int i = 0; i < n; i++) {
    if (vendor_has_types && !spoolTypeOfVendor(list[i].name, vendor)) continue;
    const float d = typeDistance(list, i, grams);
    if (!(d <= tol)) continue;
    int pos = count;
    while (pos > 0 && typeDistance(list, out[pos - 1], grams) > d) pos--;
    if (pos >= out_max) continue;   // farther than all out_max that are held
    // The slot the shift ends in: a new one while there is room, else the
    // last one, whose entry drops out.
    const int last = count < out_max ? count : out_max - 1;
    for (int k = last; k > pos; k--) out[k] = out[k - 1];
    out[pos] = i;
    if (count < out_max) count++;
  }
  return count;
}

float spoolTypeTareFromFull(float bare, float fill) {
  return bare - fill;
}

bool spoolTypeTarePlausible(float tare) {
  // Both comparisons are false for a value that is not a number.
  return tare >= NEW_SPOOL_TARE_MIN_G && tare <= NEW_SPOOL_TARE_MAX_G;
}

float spoolTypeFullLabel(float fill, float rest) {
  const float f = (isfinite(fill) && fill > 0.0f) ? fill : 0.0f;
  const float r = (isfinite(rest) && rest > 0.0f) ? rest : 0.0f;
  return r > f ? r : f;
}

FullSpoolWrite spoolTypeFullSpoolWrite(float bare, float fill, float tare) {
  const int tare_g = spoolTypeGrams(tare);
  // Against the tare the server will hold, not the one typed in: 212.3 g is
  // 212 g over there, and the rest it computes is the reading less that.
  float rest = bare - (float)tare_g;
  // The negated comparison also catches a rest that is not a number.
  if (!(rest > 0.0f) || !isfinite(rest)) rest = 0.0f;
  const int label_g = spoolTypeGrams(spoolTypeFullLabel(fill, rest));
  // The label is rounded and the rest is not, so a rest of 1000.3 g would
  // stand above a label of 1000 g. The cut costs at most half a gram.
  const float label = (float)label_g;
  const FullSpoolWrite w = { tare_g, label_g, rest < label ? rest : label };
  return w;
}

bool spoolLabelAsk(bool bambuddy, float label, float rest) {
  // A rest that is not a number fails the comparison and asks nothing.
  return bambuddy && label > 0.0f && rest > label + BB_CAP_TOLERANCE_G;
}

SpoolTypeAsk spoolTypeFirstAsk(float total, float remaining, float bare, bool own_tare, float stored_tare) {
  // 1. Nothing on the dish, or nothing a spool could be: no question. The
  //    negated comparison also stops a reading that is not a number.
  if (!(bare > NEW_SPOOL_TARE_MIN_G)) return STA_NONE;

  // 2. Only an unused spool with a label weight gives its tare away. Any
  //    other has no tare to read: it is asked from the list unless it has
  //    one of its own.
  const bool unused = total > 0.0f && (total - remaining) < SPOOL_TYPE_UNUSED_G;
  if (!unused) return own_tare ? STA_NONE : STA_LIST;

  // 3. The tare the reading gives, against the one stored for the spool.
  //    Exactly SPOOL_TYPE_DEVIATION_G off is still the same spool.
  const float tare = spoolTypeTareFromFull(bare, total);
  if (own_tare && fabsf(stored_tare - tare) <= SPOOL_TYPE_DEVIATION_G) return STA_NONE;

  // 4. Something to offer, if it can be a spool.
  return spoolTypeTarePlausible(tare) ? STA_SUGGEST : STA_IMPLAUSIBLE;
}

int spoolTypeCreateTare(int chosen_g, int chain_g, bool chain_trusted, bool* trusted) {
  // A chosen type comes first and counts as stored, even at 250 g. The test
  // for chosen_g > 0 must come before the chain: asking the chain first would
  // return its 250 g in place of a type picked for the spool.
  const bool chosen = chosen_g > 0;
  if (trusted) *trusted = chosen ? true : chain_trusted;
  return chosen ? chosen_g : chain_g;
}

int spoolTypeOwnCore(int tare_g, bool inherited) {
  // An inherited empty weight is not written onto the new spool as its own
  // (L21), so the result is 0. Otherwise the tare, never negative.
  if (inherited) return 0;
  return tare_g > 0 ? tare_g : 0;
}

bool spoolTypeTagAsk(bool bambu_tag, bool bambuddy, float bare, int type_g) {
  // Wurzel: a NaN reading compares false both ways, so it must not count as a
  // spool on the pad. Fix: the negated comparison, as in spoolTypeFirstAsk(),
  // sends NaN to "nothing on the pad". NOT-TO-DO: an early return on
  // "bare <= NEW_SPOOL_TARE_MIN_G" lets NaN through to the card.
  if (!bambu_tag || !bambuddy) return false;
  if (!(bare > NEW_SPOOL_TARE_MIN_G)) return false;
  return type_g <= 0;
}

void spoolTypeTrimName(char* dst, size_t n, const char* src) {
  if (!dst || n == 0) return;
  if (!src) src = "";
  while (*src == ' ') src++;
  size_t len = strlen(src);
  if (len > n - 1) {
    len = n - 1;
    // src[len] is the first byte that is cut. A continuation byte (10xxxxxx)
    // there means the cut would fall inside a character: back off to the
    // start of it, so that the character goes whole.
    while (len > 0 && ((unsigned char)src[len] & 0xC0) == 0x80) len--;
  }
  // memmove, so that trimming a name in place (src is dst) works too.
  memmove(dst, src, len);
  dst[len] = '\0';
  // After the cut, which may have ended on a blank.
  while (len && dst[len - 1] == ' ') dst[--len] = '\0';
}

bool spoolTypeNameTaken(const SpoolType* list, int n, const char* name) {
  if (!list || !name || n <= 0) return false;
  const char* want;
  const size_t wlen = spanTrimmed(name, strlen(name), &want);
  if (wlen == 0) return false;
  for (int i = 0; i < n; i++) {
    const char* have;
    const size_t hlen = spanTrimmed(list[i].name, (size_t)SPOOL_TYPE_NAME_MAX, &have);
    if (hlen == wlen && sameFolded(have, want, wlen)) return true;
  }
  return false;
}

int spoolTypeGrams(float g) {
  if (!isfinite(g) || g <= 0.0f) return 0;
  if (g > kGramsCap) g = kGramsCap;
  return (int)roundf(g);
}
