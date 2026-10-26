#include "services/bambuddy_name.h"

#include <string.h>
#include <strings.h>

#include "services/text_util.h"

static bool isBlank(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// The text without the blanks around it: where it starts and how many bytes
// it has. Nothing (s null or only blanks) is zero bytes.
static const char* trimmed(const char* s, size_t* len) {
  *len = 0;
  if (!s) return "";
  while (isBlank(*s)) s++;
  size_t n = strlen(s);
  while (n > 0 && isBlank(s[n - 1])) n--;
  *len = n;
  return s;
}

void bambuddyFilamentName(const char* material, const char* line, char* out, size_t out_size) {
  if (!out || !out_size) return;
  size_t m_len = 0, l_len = 0;
  const char* m = trimmed(material, &m_len);
  const char* l = trimmed(line, &l_len);

  // The material first. utf8Cut stops at m_len, which leaves the blanks behind
  // the material out, and at the buffer, never through a UTF-8 sequence.
  utf8Cut(m, m_len, out, out_size);
  const size_t used = strlen(out);
  // No line, or the buffer is full with the material: that is the name.
  if (l_len == 0 || used < m_len) return;

  // The line behind a blank (none in front of a line with no material), cut
  // the same way into what is left of the buffer.
  const size_t sep = used ? 1 : 0;
  if (used + sep >= out_size) return;
  if (sep) out[used] = ' ';
  utf8Cut(l, l_len, out + used + sep, out_size - used - sep);
  // A line that did not fit at all leaves no blank dangling behind the material.
  if (sep && !out[used + sep]) out[used] = '\0';
}

bool isClearHex(const char* hex) {
  return hex && strlen(hex) == 8 && strcmp(hex + 6, "00") == 0;
}

// A colour without the '#' in front of it, null as nothing.
static const char* bare(const char* hex) {
  if (!hex) return "";
  return *hex == '#' ? hex + 1 : hex;
}

// Whether two colours have the same six digits, whatever the case; what
// follows them (an alpha pair) is not compared. Fewer than six digits is no
// colour.
static bool sameSix(const char* a, size_t a_len, const char* b, size_t b_len) {
  return a_len >= 6 && b_len >= 6 && strncasecmp(a, b, 6) == 0;
}

// Whether two lists "9CDBD9,FFFFFF" have the same colours in the same order,
// colour by colour as sameSix() reads them. Either one empty or null: no.
static bool sameList(const char* a, const char* b) {
  if (!a || !b || !*a || !*b) return false;
  for (;;) {
    const char* a_end = strchr(a, ',');
    const char* b_end = strchr(b, ',');
    size_t a_len = a_end ? (size_t)(a_end - a) : strlen(a);
    size_t b_len = b_end ? (size_t)(b_end - b) : strlen(b);
    if (*a == '#') { a++; a_len--; }
    if (*b == '#') { b++; b_len--; }
    if (!sameSix(a, a_len, b, b_len)) return false;
    // Both end here, or neither does: one more colour on one side is no fit.
    if (!a_end || !b_end) return !a_end && !b_end;
    a = a_end + 1;
    b = b_end + 1;
  }
}

bool bambuddyColourFits(const char* fil_hex, const char* fil_multi, bool want_clear,
                        const char* want_hex, const char* want_multi) {
  // Several colours wanted: the filament's list, and only that (a filament
  // with several colours has no color_hex of its own).
  if (want_multi && *want_multi) return sameList(fil_multi, want_multi);
  // One colour or a clear one wanted: not a filament with a list.
  if (fil_multi && *fil_multi) return false;
  const char* hex = bare(fil_hex);
  if (!*hex) return false;
  if (want_clear) return isClearHex(hex);
  // A clear filament is no hue, however its six digits read.
  if (isClearHex(hex)) return false;
  const char* want = bare(want_hex);
  return sameSix(hex, strlen(hex), want, strlen(want));
}
