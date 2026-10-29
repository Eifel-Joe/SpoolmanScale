#include "services/text_fold.h"

// The code point of the sequence at s, whose first byte is not ASCII, and its
// length in bytes (2 to 4); 0 for a sequence that is broken, cut, overlong or
// a surrogate. Each byte is read only after the one before it was a
// continuation byte, so a terminator ends the read and nothing beyond it is
// touched.
static int decodeSequence(const unsigned char* s, uint32_t* cp) {
  int len;
  uint32_t v;
  uint32_t least;   // the smallest code point that needs this length
  if (s[0] < 0xC2) return 0;   // a continuation byte alone, or C0/C1 (overlong)
  if (s[0] < 0xE0)      { len = 2; v = s[0] & 0x1F; least = 0x80; }
  else if (s[0] < 0xF0) { len = 3; v = s[0] & 0x0F; least = 0x800; }
  else if (s[0] < 0xF5) { len = 4; v = s[0] & 0x07; least = 0x10000; }
  else return 0;
  for (int i = 1; i < len; i++) {
    if ((s[i] & 0xC0) != 0x80) return 0;
    v = (v << 6) | (s[i] & 0x3F);
  }
  if (v < least || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return 0;
  *cp = v;
  return len;
}

// The small letter of a capital one, every other code point as it is. The
// Latin-1 capitals (U+00C0 to U+00DE) sit 0x20 below their small letters, bar
// the multiplication sign in the middle of them; the sharp s (U+00DF) above
// them has no capital here and stays. Beyond Latin-1 only the two capitals of
// French are folded (OE ligature, Y diaeresis); any other script is kept.
static uint32_t foldCode(uint32_t cp) {
  if (cp >= 'A' && cp <= 'Z') return cp + ('a' - 'A');
  if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 0x20;
  if (cp == 0x152) return 0x153;   // OE ligature
  if (cp == 0x178) return 0xFF;    // Y with diaeresis, whose small letter is in Latin-1
  return cp;
}

const char* textFoldNext(const char* s, uint32_t* folded) {
  uint32_t scratch;
  if (!folded) folded = &scratch;
  if (!s || !*s) {
    *folded = 0;
    return s;
  }
  const unsigned char* u = (const unsigned char*)s;
  if (u[0] < 0x80) {
    *folded = foldCode(u[0]);
    return s + 1;
  }
  uint32_t cp = 0;
  const int len = decodeSequence(u, &cp);
  if (len == 0) {
    *folded = u[0];   // one byte, as it is: never folded, never skipped
    return s + 1;
  }
  *folded = foldCode(cp);
  return s + len;
}
