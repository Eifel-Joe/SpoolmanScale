#pragma once

#include <stdint.h>
#include <string.h>
#include <strings.h>

// ============================================================
//  WHETHER AN ANSWER CAME FROM THE SCALE'S OWN SERVER
//
//  The refusal record (server_refusal.h) and the acceptance's made-up answer
//  are about the server the scale is set up for, and the same client also
//  asks GitHub, Spoolman's database and, in this fork, Klipper printers. The
//  original asked strstr(backendBaseUrl(), host): a Moonraker on the Pi that
//  also runs Spoolman (7125 beside 7912) counted as the server, so did a host
//  that is only the start of another one, and an empty host - a begin() that
//  failed - matched every address. Same host and same port, and nothing else.
//  No switch, the host test compiles it (test_own_server_rules.py).
// ============================================================

// base_url as the backend keeps it ("http://host:port[/path]"), host and port
// as HTTPClient took them from the request's URL.
inline bool sameServerAs(const char* base_url, const char* host, uint16_t port) {
  if (!base_url || !host || !*host) return false;
  uint16_t want_port = 0;
  const char* p = nullptr;
  if (strncasecmp(base_url, "http://", 7) == 0) {
    p = base_url + 7;
    want_port = 80;
  } else if (strncasecmp(base_url, "https://", 8) == 0) {
    p = base_url + 8;
    want_port = 443;
  } else {
    return false;
  }
  const char* end = p;
  while (*end && *end != '/' && *end != ':') end++;
  const size_t len = (size_t)(end - p);
  if (len == 0 || strlen(host) != len || strncasecmp(p, host, len) != 0) return false;
  if (*end == ':') {
    uint32_t n = 0;
    const char* d = end + 1;
    if (*d < '0' || *d > '9') return false;
    for (; *d >= '0' && *d <= '9'; d++) {
      n = n * 10 + (uint32_t)(*d - '0');
      if (n > 65535) return false;
    }
    want_port = (uint16_t)n;
  }
  return want_port == port;
}
