#include "services/klipper_location_rules.h"

#include <string.h>

namespace {

// The whole of src with its terminator, or nothing: false (out "") when it
// does not fit n. A location cut short would be another location.
bool copyWhole(const char* src, char* out, size_t n) {
  const size_t len = strlen(src);
  if (len >= n) {
    out[0] = '\0';
    return false;
  }
  memcpy(out, src, len + 1);
  return true;
}

// Byte for byte, the way the spec says "exactly the printer's name".
bool isPrinterName(const char* location, KlipperNames printers) {
  if (!location[0] || !printers.name) return false;
  for (int i = 0; i < printers.count; i++) {
    if (printers.name[i] && strcmp(printers.name[i], location) == 0) return true;
  }
  return false;
}

// A text written into n bytes. ok turns false at the first piece that does
// not fit together with the terminator; nothing is written after that.
struct Writer {
  char* out;
  size_t n;
  size_t len;
  bool ok;
};

void put(Writer* w, const char* s, size_t k) {
  if (!w->ok) return;
  if (w->len + k >= w->n) {
    w->ok = false;
    return;
  }
  memcpy(w->out + w->len, s, k);
  w->len += k;
}

// Terminates what was written, or leaves "" when something did not fit.
bool finish(Writer* w) {
  w->out[w->ok ? w->len : 0] = '\0';
  return w->ok;
}

const char kHexDigits[] = "0123456789abcdef";

void putEscaped(Writer* w, unsigned char c) {
  switch (c) {
    case '"':  put(w, "\\\"", 2); return;
    case '\\': put(w, "\\\\", 2); return;
    case '\n': put(w, "\\n", 2); return;
    case '\r': put(w, "\\r", 2); return;
    case '\t': put(w, "\\t", 2); return;
    case '\b': put(w, "\\b", 2); return;
    case '\f': put(w, "\\f", 2); return;
    default: break;
  }
  if (c < 0x20) {
    const char u[6] = {'\\', 'u', '0', '0', kHexDigits[c >> 4], kHexDigits[c & 0x0f]};
    put(w, u, sizeof(u));
    return;
  }
  const char one = static_cast<char>(c);
  put(w, &one, 1);
}

// Four hex digits at p as a number, -1 when they are not. It stops at the
// first character that is no digit, so a terminator is never read past.
long hex4(const char* p) {
  long v = 0;
  for (int i = 0; i < 4; i++) {
    const char c = p[i];
    int d = 0;
    if (c >= '0' && c <= '9') {
      d = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      d = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
      d = c - 'A' + 10;
    } else {
      return -1;
    }
    v = v * 16 + d;
  }
  return v;
}

void putUtf8(Writer* w, unsigned long cp) {
  char b[4];
  size_t k = 0;
  if (cp < 0x80) {
    b[k++] = static_cast<char>(cp);
  } else if (cp < 0x800) {
    b[k++] = static_cast<char>(0xC0 | (cp >> 6));
    b[k++] = static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    b[k++] = static_cast<char>(0xE0 | (cp >> 12));
    b[k++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    b[k++] = static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    b[k++] = static_cast<char>(0xF0 | (cp >> 18));
    b[k++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    b[k++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    b[k++] = static_cast<char>(0x80 | (cp & 0x3F));
  }
  put(w, b, k);
}

// \uXXXX with p at the first digit, a pair of surrogates as one character:
// the position after it, or null for what JSON does not allow here (fewer
// than four digits, half a pair) and for \u0000, which would end the text.
const char* unicodeEscape(const char* p, Writer* w) {
  long cp = hex4(p);
  if (cp <= 0) return nullptr;
  p += 4;
  if (cp >= 0xDC00 && cp <= 0xDFFF) return nullptr;
  if (cp >= 0xD800 && cp <= 0xDBFF) {
    if (p[0] != '\\' || p[1] != 'u') return nullptr;
    const long low = hex4(p + 2);
    if (low < 0xDC00 || low > 0xDFFF) return nullptr;
    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
    p += 6;
  }
  putUtf8(w, static_cast<unsigned long>(cp));
  return p;
}

// The escape whose letter is at p (the backslash is behind): the position
// after it, or null for an escape JSON does not have.
const char* unescape(const char* p, Writer* w) {
  char c = 0;
  switch (*p) {
    case '"':  c = '"'; break;
    case '\\': c = '\\'; break;
    case '/':  c = '/'; break;
    case 'b':  c = '\b'; break;
    case 'f':  c = '\f'; break;
    case 'n':  c = '\n'; break;
    case 'r':  c = '\r'; break;
    case 't':  c = '\t'; break;
    case 'u':  return unicodeEscape(p + 1, w);
    default:   return nullptr;
  }
  put(w, &c, 1);
  return p + 1;
}

bool isBlank(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

}  // namespace

bool klipperPrevToRemember(const char* current_location, const char* stored_prev,
                           KlipperNames printers, char* out, size_t n) {
  if (!out || n == 0) return false;
  const char* current = current_location ? current_location : "";
  const char* keep = current;
  if (isPrinterName(current, printers)) keep = stored_prev ? stored_prev : "";
  return copyWhole(keep, out, n);
}

KlipperRestoreAct klipperRestoreFor(const char* displaced_location, const char* printer_name,
                                    const char* stored_prev) {
  if (!displaced_location || !printer_name || !printer_name[0]) return KR_NONE;
  if (strcmp(displaced_location, printer_name) != 0) return KR_NONE;
  return (stored_prev && stored_prev[0]) ? KR_RESTORE : KR_CLEAR;
}

bool klipperExtraText(const char* value, char* out, size_t n) {
  if (!out || n == 0) return false;
  Writer w = {out, n, 0, true};
  put(&w, "\"", 1);
  for (const char* p = value ? value : ""; *p; p++) putEscaped(&w, static_cast<unsigned char>(*p));
  put(&w, "\"", 1);
  return finish(&w);
}

bool klipperExtraValue(const char* json_text, char* out, size_t n) {
  if (!out || n == 0) return false;
  out[0] = '\0';
  if (!json_text) return false;
  const char* p = json_text;
  while (isBlank(*p)) p++;
  if (*p != '"') return false;
  p++;
  Writer w = {out, n, 0, true};
  for (;;) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c == '"') break;
    if (c < 0x20) {             // the terminator before the closing quote, or a raw control character
      w.ok = false;
      return finish(&w);
    }
    if (c == '\\') {
      p = unescape(p + 1, &w);
      if (!p) {
        w.ok = false;
        return finish(&w);
      }
      continue;
    }
    put(&w, p, 1);
    p++;
  }
  p++;
  while (isBlank(*p)) p++;
  if (*p != '\0') w.ok = false;
  return finish(&w);
}
