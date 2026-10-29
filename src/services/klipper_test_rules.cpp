#include "services/klipper_test_rules.h"

#include <string.h>

namespace {

// The room of an address as it is compared: "https://" and ":65535" take 15
// bytes with the terminator, the rest is the host. A host that does not fit
// is no address (klipperSpoolmanAddr gives false).
const size_t ADDR_MAX = 128;

char lowerAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

bool isBlank(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// True when [s, s + n) is the lower-case word, in any case.
bool equalNoCase(const char* s, size_t n, const char* word) {
  if (strlen(word) != n) return false;
  for (size_t i = 0; i < n; i++) {
    if (lowerAscii(s[i]) != word[i]) return false;
  }
  return true;
}

// The characters of a host name or an IPv4 address. Nothing else is
// accepted: a user name ('@'), an IPv6 form ('[', more than one ':') and a
// query are not told apart reliably, so they are no address.
bool isHostChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
         c == '.' || c == '-' || c == '_';
}

// The digits of v (a port, five at most), most significant first, without a
// terminator; returns how many. No snprintf: nothing here can raise a
// truncation warning.
size_t decimal(unsigned v, char* out) {
  char rev[8];
  size_t k = 0;
  do {
    rev[k++] = (char)('0' + v % 10u);
    v /= 10u;
  } while (v != 0 && k < sizeof(rev));
  for (size_t i = 0; i < k; i++) out[i] = rev[k - 1 - i];
  return k;
}

// Cuts a trailing "/" and then a trailing "/api" (with the slashes behind
// it) off [path, *end), as often as they are there, and says whether
// anything is left. What is left is a path of the server (a proxy under
// "/spoolman", say), which the address form has no place for.
bool pathIsBare(const char* path, const char** end) {
  const char* e = *end;
  for (;;) {
    while (e > path && e[-1] == '/') e--;
    if (e - path >= 4 && e[-4] == '/' && equalNoCase(e - 3, 3, "api")) {
      e -= 4;
      continue;
    }
    break;
  }
  *end = e;
  return e == path;
}

}  // namespace

bool klipperSpoolmanAddr(const char* url, char* out, size_t n) {
  if (!out || n == 0) return false;
  out[0] = '\0';
  if (!url) return false;

  const char* begin = url;
  while (*begin != '\0' && isBlank(*begin)) begin++;
  const char* end = begin + strlen(begin);
  while (end > begin && isBlank(end[-1])) end--;

  // A schema stands in front of the first slash and is followed by "//".
  // Looked for there only, so that a "://" in a path or query is not taken
  // for one.
  const char* slash = begin;
  while (slash < end && *slash != '/') slash++;
  bool https = false;
  const char* rest = begin;
  if (end - slash >= 2 && slash[1] == '/' && slash > begin && slash[-1] == ':') {
    const size_t schema_len = (size_t)(slash - 1 - begin);
    if (equalNoCase(begin, schema_len, "https")) {
      https = true;
    } else if (!equalNoCase(begin, schema_len, "http")) {
      return false;
    }
    rest = slash + 2;
  }

  // The address is host[:port]; what follows its first slash is the path.
  const char* auth_end = rest;
  while (auth_end < end && *auth_end != '/') auth_end++;
  const char* path_end = end;
  if (!pathIsBare(auth_end, &path_end)) return false;

  const char* host_end = rest;
  while (host_end < auth_end && *host_end != ':') host_end++;
  const size_t host_len = (size_t)(host_end - rest);
  if (host_len == 0) return false;
  for (size_t i = 0; i < host_len; i++) {
    if (!isHostChar(rest[i])) return false;
  }

  unsigned port = https ? 443u : 80u;
  if (host_end < auth_end) {
    const char* d = host_end + 1;
    if (d == auth_end) return false;   // "host:" says no port
    unsigned v = 0;
    for (; d < auth_end; d++) {
      if (*d < '0' || *d > '9') return false;
      v = v * 10u + (unsigned)(*d - '0');
      if (v > 65535u) return false;
    }
    if (v == 0) return false;
    port = v;
  }

  char digits[8];
  const size_t digit_count = decimal(port, digits);
  const size_t schema_out = https ? 5 : 4;
  if (schema_out + 3 + host_len + 1 + digit_count + 1 > n) return false;

  size_t o = 0;
  memcpy(out, https ? "https" : "http", schema_out);
  o += schema_out;
  memcpy(out + o, "://", 3);
  o += 3;
  for (size_t i = 0; i < host_len; i++) out[o++] = lowerAscii(rest[i]);
  out[o++] = ':';
  memcpy(out + o, digits, digit_count);
  o += digit_count;
  out[o] = '\0';
  return true;
}

bool klipperSameAddr(const char* a, const char* b) {
  char ka[ADDR_MAX];
  char kb[ADDR_MAX];
  if (!klipperSpoolmanAddr(a, ka, sizeof(ka))) return false;
  if (!klipperSpoolmanAddr(b, kb, sizeof(kb))) return false;
  return strcmp(ka, kb) == 0;
}

KlipperSameDb klipperSameNewest(int a_id, const char* a_registered, int b_id, const char* b_registered) {
  // Not read is not "different": nothing is known, and the caller decides
  // what that is worth.
  if (a_id < 0 || b_id < 0) return KS_UNKNOWN;
  // Both empty is the same. Two empty databases cannot be told apart, and
  // with no spool on either there is no number that could mean one spool
  // here and another there. NOT-TO-DO: reading it as "unknown": a printer on
  // a fresh Spoolman beside a fresh Spoolman would be refused for nothing.
  if (a_id == 0 && b_id == 0) return KS_SAME;
  if (a_id == 0 || b_id == 0) return KS_OTHER;
  if (a_id != b_id) return KS_OTHER;
  const char* a = a_registered ? a_registered : "";
  const char* b = b_registered ? b_registered : "";
  return strcmp(a, b) == 0 ? KS_SAME : KS_OTHER;
}

KlipperTest klipperTestVerdict(const KlipperProbe& p) {
  if (p.access_code <= 0) return KT_UNREACHABLE;
  if (p.info_code == 401 || p.info_code == 403) return KT_ACCESS;
  if (p.info_code != 200) return KT_BAD_ANSWER;
  // The state is read no further than its array: a text that lost its
  // terminator is not "ready".
  if (strncmp(p.klippy_state, "ready", sizeof(p.klippy_state)) != 0) return KT_KLIPPY;
  if (!p.has_spoolman) return KT_NO_SPOOLMAN;
  if (p.status_code != 200 || !p.sm_connected) return KT_SM_OFFLINE;
  // The same address wins over a newest spool that differs (a spool added
  // between the two questions). Another address is the same Spoolman only
  // when the newest spools agree; "could not tell" is not agreement.
  if (!p.addr_same && p.newest != KS_SAME) return KT_OTHER_SPOOLMAN;
  return KT_OK;
}
