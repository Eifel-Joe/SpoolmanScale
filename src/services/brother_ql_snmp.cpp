#include "services/brother_ql.h"

#include <Arduino.h>
#include <WiFi.h>
#include <errno.h>
#include <lwip/sockets.h>
#include <string.h>
#include <unistd.h>

#include "hardware/sd_logger.h"
#include "services/breadcrumb.h"

// ============================================================
//  THE BROTHER'S STATUS OVER SNMP (Spec 5.3 L1, V78)
//
//  One UDP socket per question, closed again: the GET that
//  brother_ql_status.h builds goes to port 161 of the printer, and
//  the first datagram from the printer's address that reads as the
//  GetResponse to it is the answer. A late answer to the first try
//  carries the first try's number and is passed over (its parse
//  fails), so it cannot stand in for the second.
//
//  Wurzel of the whole check: the raw port 9100 says nothing back
//  (befunde 1 of beta.8, STATUS 49 "never bytes from the printer"),
//  so a label for the wrong roll went out on 06.11.2026 and the
//  scale said "sent" while the printer refused it.
//  NOT-TO-DO: ESC i S on port 9100 (0 bytes, twice, on Joerg's
//  QL-820NWBc); a task of its own (L1, V27: the loop prints); a
//  third try (V78: the label must not wait more than 1 s for it).
// ============================================================

// The request numbers run on from one question to the next, so an answer to
// an earlier question is never taken for this one's. Kept above 0.
static uint16_t s_request = 0;
static uint16_t nextRequest() {
  if (++s_request == 0) s_request = 1;
  return s_request;
}

static const char* mediaName(uint8_t media) {
  switch (media) {
    case BQS_NONE:       return "none";
    case BQS_CONTINUOUS: return "continuous";
    case BQS_DIE_CUT:    return "die-cut";
    default:             return "unknown type";
  }
}

bool brotherQlAskStatus(const char* host, BrotherQlStatus* out) {
  if (!out || !brotherQlHostValid(host) || WiFi.status() != WL_CONNECTED) return false;
  // The address as brotherQlPrint() takes it; an IPv4 IPAddress gives its
  // four bytes in network order, as WiFiUDP hands them to the socket.
  IPAddress ip;
  if (!ip.fromString(host)) return false;
  struct sockaddr_in to;
  memset(&to, 0, sizeof(to));
  to.sin_family      = AF_INET;
  to.sin_port        = htons(BROTHER_QL_SNMP_PORT);
  to.sin_addr.s_addr = (uint32_t)ip;

  const int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) {
    logSDf("Printer: Brother %s status not asked, no socket (errno %d)", host, errno);
    return false;
  }
  crumbSet("brother status");
  const uint32_t started = millis();
  uint8_t req[BROTHER_QL_SNMP_REQUEST_MAX];
  uint8_t ans[BROTHER_QL_SNMP_ANSWER_MAX];
  bool got = false;
  int tries = 0;
  for (; tries < BROTHER_QL_STATUS_TRIES && !got; tries++) {
    const uint16_t id = nextRequest();
    const size_t n = brotherQlStatusRequest(id, req, sizeof(req));
    if (!n || sendto(fd, req, n, 0, (struct sockaddr*)&to, sizeof(to)) != (int)n) continue;
    const uint32_t sent = millis();
    for (;;) {
      const uint32_t waited = millis() - sent;
      if (waited >= BROTHER_QL_STATUS_WAIT_MS) break;
      const uint32_t left = BROTHER_QL_STATUS_WAIT_MS - waited;
      fd_set readable;
      FD_ZERO(&readable);
      FD_SET(fd, &readable);
      struct timeval tv;
      tv.tv_sec  = 0;
      tv.tv_usec = (long)left * 1000L;
      if (select(fd + 1, &readable, nullptr, nullptr, &tv) <= 0) break;
      struct sockaddr_in from;
      socklen_t from_len = sizeof(from);
      const int r = recvfrom(fd, ans, sizeof(ans), 0, (struct sockaddr*)&from, &from_len);
      if (r < 0) break;
      // Only the printer's own datagrams, from its SNMP port, count; an
      // empty one or a stranger's leaves the wait running.
      if (r == 0 || from.sin_addr.s_addr != to.sin_addr.s_addr || from.sin_port != to.sin_port)
        continue;
      if (brotherQlStatusParse(ans, (size_t)r, id, out)) {
        got = true;
        break;
      }
    }
  }
  close(fd);
  crumbSet("loop");

  const unsigned ms = (unsigned)(millis() - started);
  if (got)
    logSDf("Printer: Brother %s status: roll %ux%u %s, errors %02X %02X, model %02X, %u ms, try %d",
           host, (unsigned)out->width_mm, (unsigned)out->length_mm, mediaName(out->media),
           (unsigned)out->err1, (unsigned)out->err2, (unsigned)out->model, ms, tries);
  else
    logSDf("Printer: Brother %s status: no answer in %u ms (%d tries), roll not checked",
           host, ms, tries);
  return got;
}
