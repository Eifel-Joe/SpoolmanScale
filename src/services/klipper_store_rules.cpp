#include "services/klipper_store_rules.h"

#include <stdio.h>
#include <string.h>

#include "services/accent_fold.h"
#include "services/text_util.h"

namespace {

const uint32_t RECORD_VERSION = 1;
const uint32_t U32_MAX = 0xFFFFFFFFu;

// ------------------------------------------------------------
//  Small text helpers
// ------------------------------------------------------------

char lowerAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// A blank at the end of what was typed or pasted. A tab or a line break
// comes with a copied address as often as a space does.
bool isBlank(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// A control character: below the blank, or DEL.
bool isControl(unsigned char c) {
  return c < 0x20 || c == 0x7F;
}

// The length of an array text that may have lost its terminator: at most max.
size_t boundedLen(const char* s, size_t max) {
  size_t n = 0;
  while (n < max && s[n] != '\0') n++;
  return n;
}

bool hasControl(const char* s, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (isControl((unsigned char)s[i])) return true;
  }
  return false;
}

// True when [s, end) begins with the lower-case prefix, in any case.
bool prefixNoCase(const char* s, const char* end, const char* prefix) {
  for (; *prefix != '\0'; prefix++, s++) {
    if (s >= end || lowerAscii(*s) != *prefix) return false;
  }
  return true;
}

// The digits of v, most significant first, without a terminator; returns how
// many (at most 10). No snprintf: nothing here can raise a truncation warning.
size_t decimal(uint32_t v, char* out) {
  char rev[12];
  size_t k = 0;
  do {
    rev[k++] = (char)('0' + v % 10u);
    v /= 10u;
  } while (v != 0 && k < sizeof(rev));
  for (size_t i = 0; i < k; i++) out[i] = rev[k - 1 - i];
  return k;
}

// The characters a key (and so the list of keys) may be made of.
bool keyChars(const char* s, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const char c = s[i];
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

// ------------------------------------------------------------
//  The record
// ------------------------------------------------------------

// Reads the plain decimal number at *p and moves *p behind it. It must end
// at a tab (another field follows) or, for the last field, at the end of
// the text; a sign, a blank, a letter or a number past 32 bit is no field.
bool takeNumber(const char** p, bool last, uint32_t* v) {
  const char* s = *p;
  uint64_t acc = 0;
  int digits = 0;
  while (*s >= '0' && *s <= '9') {
    acc = acc * 10u + (uint64_t)(*s - '0');
    if (acc > U32_MAX) return false;
    s++;
    digits++;
  }
  if (digits == 0) return false;
  if (last ? (*s != '\0') : (*s != '\t')) return false;
  *v = (uint32_t)acc;
  *p = last ? s : s + 1;
  return true;
}

// Copies the text field at *p, up to the next tab, into dst and moves *p
// behind the tab. It must not be empty and must fit cap with its terminator.
// Every text field is followed by another one, so the end of the text is no
// end of a field here.
bool takeText(const char** p, char* dst, size_t cap) {
  const char* s = *p;
  size_t n = 0;
  while (s[n] != '\0' && s[n] != '\t') n++;
  if (s[n] != '\t' || n == 0 || n >= cap) return false;
  memcpy(dst, s, n);
  dst[n] = '\0';
  *p = s + n + 1;
  return true;
}

// ------------------------------------------------------------
//  The list of keys
// ------------------------------------------------------------

struct Entry {
  uint32_t id;
  const char* key;
  size_t key_len;
};

// Reads the next entry "<id>=<key>" of the list at *p and moves *p behind its
// separator. False at the end of the list. A piece that is no entry (no '=',
// no number before it, a number past 32 bit, an empty key) is stepped over:
// an empty key is how an entry is taken away, so it never stands in the list.
bool nextEntry(const char** p, Entry* e) {
  while (**p != '\0') {
    const char* s = *p;
    const char* end = s;
    while (*end != '\0' && *end != ';') end++;
    *p = (*end == ';') ? end + 1 : end;

    uint64_t id = 0;
    const char* q = s;
    while (q < end && *q >= '0' && *q <= '9' && id <= U32_MAX) {
      id = id * 10u + (uint64_t)(*q - '0');
      q++;
    }
    if (q == s || q >= end || *q != '=' || id > U32_MAX || q + 1 == end) continue;
    e->id = (uint32_t)id;
    e->key = q + 1;
    e->key_len = (size_t)(end - (q + 1));
    return true;
  }
  return false;
}

// The list as it is built: every part is checked against the room, and the
// text is terminated after each one, so that it is a text at any point.
struct ListOut {
  char* dst;
  size_t cap;
  size_t len;
  bool ok;
};

void listAdd(ListOut* o, const char* s, size_t n) {
  if (!o->ok) return;
  if (o->len + n + 1 > o->cap) {
    o->ok = false;
    return;
  }
  memcpy(o->dst + o->len, s, n);
  o->len += n;
  o->dst[o->len] = '\0';
}

void listEntry(ListOut* o, uint32_t id, const char* key, size_t key_len) {
  if (o->len > 0) listAdd(o, ";", 1);
  char digits[12];
  const size_t digit_count = decimal(id, digits);
  listAdd(o, digits, digit_count);
  listAdd(o, "=", 1);
  listAdd(o, key, key_len);
}

// ------------------------------------------------------------
//  The name as it is compared
// ------------------------------------------------------------

// The room of the folded name. Stored names are shorter than KL_NAME_MAX and
// a fold never makes a text longer, so this is never reached by a cleaned
// name; a longer one would be compared by what fits.
const size_t NAME_KEY_MAX = 64;

// The text two names are compared by: the accents folded onto their base
// letters (accentFold, the fold the label printer uses), then the case. The
// letters accentFold leaves alone are the ones the fonts have: the umlauts
// a, o, u and their capitals, so the capitals of Latin-1 (U+00C0 to U+00DE
// except the multiplication sign) are lowered here, as textFoldNext() does.
//
// NOT-TO-DO: textFoldNext() itself. Its unit (text_fold.cpp) is not part of
// the host test of this one, and it would add a third fold to the two that
// are already there. The result of this one only has to agree with itself.
void nameKey(const char* name, char* key, size_t cap) {
  accentFold(name, key, cap);
  for (unsigned char* p = (unsigned char*)key; *p != '\0'; p++) {
    if (*p >= 'A' && *p <= 'Z') {
      *p = (unsigned char)(*p - 'A' + 'a');
    } else if (*p == 0xC3 && p[1] >= 0x80 && p[1] <= 0x9E && p[1] != 0x97) {
      p++;
      *p = (unsigned char)(*p + 0x20);
    }
  }
}

}  // namespace

// ------------------------------------------------------------
//  The record
// ------------------------------------------------------------

bool klipperRecordEncode(const KlipperPrinterCfg& c, char* out, size_t n) {
  if (!out || n == 0) return false;
  out[0] = '\0';
  // The arrays are looked at no further than they are long; one that has
  // lost its terminator fills the whole array and is refused with the rest.
  const size_t name_len = boundedLen(c.name, sizeof(c.name));
  const size_t host_len = boundedLen(c.host, sizeof(c.host));
  if (c.id < (uint32_t)KL_ID_BASE || c.port == 0) return false;
  if (name_len == 0 || name_len >= sizeof(c.name)) return false;
  if (host_len == 0 || host_len >= sizeof(c.host)) return false;
  if (hasControl(c.name, name_len) || hasControl(c.host, host_len)) return false;

  const int r = snprintf(out, n, "%lu\t%lu\t%s\t%s\t%u\t%u\t%u\t%u\t%u\t%lu",
                         (unsigned long)RECORD_VERSION, (unsigned long)c.id, c.name, c.host,
                         (unsigned)c.port, (unsigned)c.last.test, (unsigned)c.last.layout,
                         (unsigned)c.last.fields, (unsigned)c.last.flags,
                         (unsigned long)c.last.at);
  if (r < 0 || (size_t)r >= n) {
    out[0] = '\0';
    return false;
  }
  return true;
}

bool klipperRecordDecode(const char* rec, KlipperPrinterCfg* out) {
  if (!rec || !out) return false;
  KlipperPrinterCfg c = {};   // zero first: the texts are terminated whatever takeText copies
  const char* p = rec;
  uint32_t v = 0;

  if (!takeNumber(&p, false, &v) || v != RECORD_VERSION) return false;
  if (!takeNumber(&p, false, &v) || v < (uint32_t)KL_ID_BASE) return false;
  c.id = v;
  if (!takeText(&p, c.name, sizeof(c.name))) return false;
  if (!takeText(&p, c.host, sizeof(c.host))) return false;
  if (!takeNumber(&p, false, &v) || v == 0 || v > 65535u) return false;
  c.port = (uint16_t)v;
  if (!takeNumber(&p, false, &v) || v > 255u) return false;
  c.last.test = (uint8_t)v;
  if (!takeNumber(&p, false, &v) || v > 255u) return false;
  c.last.layout = (uint8_t)v;
  if (!takeNumber(&p, false, &v) || v > 255u) return false;
  c.last.fields = (uint8_t)v;
  if (!takeNumber(&p, false, &v) || v > 255u) return false;
  c.last.flags = (uint8_t)v;
  if (!takeNumber(&p, true, &v)) return false;
  c.last.at = v;

  *out = c;
  return true;
}

// ------------------------------------------------------------
//  Name, address, key
// ------------------------------------------------------------

bool klipperNameClean(const char* in, char* out, size_t n) {
  if (!out || n == 0) return false;
  out[0] = '\0';
  if (!in) return false;

  // Controls and the tab go, then the blanks at the front. One byte more than
  // the name may hold is kept: utf8Cut looks at the byte behind the cut to
  // tell whether the cut falls inside a character, and drops that character
  // whole instead of leaving half of it.
  char kept[KL_NAME_MAX + 1];
  size_t len = 0;
  for (const unsigned char* p = (const unsigned char*)in; *p != '\0' && len < (size_t)KL_NAME_MAX; p++) {
    if (isControl(*p)) continue;
    if (*p == ' ' && len == 0) continue;
    kept[len++] = (char)*p;
  }
  kept[len] = '\0';

  const size_t cap = n < (size_t)KL_NAME_MAX ? n : (size_t)KL_NAME_MAX;   // room of out, terminator included
  utf8Cut(kept, cap - 1, out, cap);

  // The cut can leave a blank at the end ("Zweihundert Zwei" cut after the
  // blank): "Voron " and "Voron" would be two names for one printer.
  size_t end = strlen(out);
  while (end > 0 && out[end - 1] == ' ') out[--end] = '\0';
  return end > 0;
}

bool klipperNameTaken(const char* name, const char* const* names, int count, int skip) {
  if (!name || !names) return false;
  char want[NAME_KEY_MAX];
  nameKey(name, want, sizeof(want));
  if (want[0] == '\0') return false;
  for (int i = 0; i < count; i++) {
    if (i == skip || !names[i]) continue;
    char have[NAME_KEY_MAX];
    nameKey(names[i], have, sizeof(have));
    if (strcmp(want, have) == 0) return true;
  }
  return false;
}

bool klipperHostClean(const char* in, char* out, size_t n) {
  if (!out || n == 0) return false;
  out[0] = '\0';
  if (!in) return false;

  const char* begin = in;
  while (*begin != '\0' && isBlank(*begin)) begin++;
  const char* end = begin + strlen(begin);
  while (end > begin && isBlank(end[-1])) end--;

  // The scale talks plain http to Moonraker, so an https address would have
  // to be refused later anyway; it is refused here, where it is typed.
  if (prefixNoCase(begin, end, "https://")) return false;
  if (prefixNoCase(begin, end, "http://")) begin += 7;
  while (end > begin && end[-1] == '/') end--;

  const size_t cap = n < (size_t)KL_HOST_MAX ? n : (size_t)KL_HOST_MAX;   // room of out, terminator included
  const size_t len = (size_t)(end - begin);
  if (len == 0 || len >= cap) return false;
  // Anything up to and including the blank is refused, not only the blank:
  // a tab in the host would split the record it is stored in. A ':' is
  // refused too: the port is a field of its own (R16), so "host:7125" would
  // become "host:7125:7125" when the address is put together, and an IPv6
  // form is not supported.
  for (size_t i = 0; i < len; i++) {
    const unsigned char c = (unsigned char)begin[i];
    if (c <= 0x20 || c == 0x7F || c == '/' || c == ':') return false;
  }
  memcpy(out, begin, len);
  out[len] = '\0';
  return true;
}

bool klipperKeyClean(const char* in, char* out, size_t n) {
  if (!out || n == 0) return false;
  out[0] = '\0';
  if (!in) return false;

  const size_t cap = n < (size_t)KL_KEY_MAX ? n : (size_t)KL_KEY_MAX;   // room of out, terminator included
  const size_t len = strlen(in);
  if (len >= cap || !keyChars(in, len)) return false;
  memcpy(out, in, len + 1);
  return true;
}

// ------------------------------------------------------------
//  The list of keys
// ------------------------------------------------------------

bool klipperKeysGet(const char* keys, uint32_t id, char* out, size_t n) {
  if (!out || n == 0) return false;
  out[0] = '\0';
  if (!keys) return false;

  const char* p = keys;
  Entry e = {};
  while (nextEntry(&p, &e)) {
    if (e.id != id) continue;
    if (e.key_len >= n) return false;
    memcpy(out, e.key, e.key_len);
    out[e.key_len] = '\0';
    return true;
  }
  return false;
}

bool klipperKeysPut(const char* keys, uint32_t id, const char* key, char* out, size_t n) {
  if (!out || n == 0) return false;
  out[0] = '\0';
  if (!key) return false;
  // A ';' or '=' in a key would split the list the next time it is read.
  const size_t key_len = strlen(key);
  if (key_len >= (size_t)KL_KEY_MAX || !keyChars(key, key_len)) return false;

  ListOut list = {out, n, 0, true};
  bool placed = false;
  const char* p = keys ? keys : "";
  Entry e = {};
  while (nextEntry(&p, &e)) {
    if (e.id != id) {
      listEntry(&list, e.id, e.key, e.key_len);
    } else if (!placed) {
      // The new key stands where the old one stood; a doubled entry of the
      // same printer further on is dropped.
      placed = true;
      if (key_len > 0) listEntry(&list, id, key, key_len);
    }
  }
  if (!placed && key_len > 0) listEntry(&list, id, key, key_len);

  if (!list.ok) {
    out[0] = '\0';
    return false;
  }
  return true;
}

// ------------------------------------------------------------
//  Room and next id
// ------------------------------------------------------------

int klipperRoomLeft(int stored, uint32_t available_entries) {
  // available_entries is unsigned: 50 free entries less the reserve of 64
  // would wrap to a number near 4 billion, and the page would offer room for
  // 64 printers on a memory that is nearly full. So the reserve is compared
  // first and only taken off what is there.
  // NOT-TO-DO: (int)(available_entries - KL_NVS_RESERVE) / ...: the wrap is
  // done before the cast.
  // See test_klipper_store_rules.py ("less than the reserve").
  const uint32_t usable = available_entries > (uint32_t)KL_NVS_RESERVE
                              ? (available_entries - (uint32_t)KL_NVS_RESERVE) / (uint32_t)KL_NVS_PER_PRINTER
                              : 0u;
  const int by_memory = usable > (uint32_t)KL_PRINTERS_MAX ? KL_PRINTERS_MAX : (int)usable;
  // stored is cut into 0..KL_PRINTERS_MAX so that the subtraction cannot overflow.
  const int have = stored < 0 ? 0 : (stored > KL_PRINTERS_MAX ? KL_PRINTERS_MAX : stored);
  const int by_count = KL_PRINTERS_MAX - have;
  return by_count < by_memory ? by_count : by_memory;
}

uint32_t klipperNextId(uint32_t stored_next, const uint32_t* ids, int count) {
  // 64 bit so that one more than the largest possible id does not wrap to 0.
  uint64_t next = (uint64_t)KL_ID_BASE + 1u;
  if (stored_next > next) next = stored_next;
  if (ids) {
    for (int i = 0; i < count; i++) {
      if ((uint64_t)ids[i] + 1u > next) next = (uint64_t)ids[i] + 1u;
    }
  }
  return next > U32_MAX ? U32_MAX : (uint32_t)next;
}
