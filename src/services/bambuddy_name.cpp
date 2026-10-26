#include "services/bambuddy_name.h"

#include <string.h>

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
