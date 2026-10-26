#include "services/brother_ql.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <errno.h>
#include <esp_heap_caps.h>
#include <lwip/sockets.h>
#include <string.h>

#include "hardware/sd_logger.h"
#include "services/breadcrumb.h"

// How long a connect may take. A printer that is off or on another network
// must be given up on quickly: the loop stands still meanwhile.
#define BQ_CONNECT_MS     3000
// The least time between two looks at the held line.
#define BQ_POLL_MS          50

// The null bytes in front of a job, from flash: a block of 100 written four
// times, so no buffer of 400 bytes ever sits in RAM.
static const uint8_t NULL_BLOCK[100] = {0};
static_assert(BROTHER_QL_INVALIDATE % sizeof(NULL_BLOCK) == 0,
              "the null bytes must be a whole number of blocks");
// One buffer serves for the 38 start bytes and for every row.
static_assert(BROTHER_QL_LINE_BYTES >= BROTHER_QL_START_BYTES,
              "the row buffer must hold the start bytes");
// The log lines say "10 s" in words, so the two go together.
static_assert(BROTHER_QL_HOLD_MS == 10000, "the log text says 10 s");

static volatile BleSessionPhase s_phase = BLE_PHASE_IDLE;
static size_t s_sent = 0, s_total = 0;

// The line to the printer. At file scope rather than in runJob(): it outlives
// the job by the hold, and the loop looks at it from brotherQlTick().
static WiFiClient s_line;

// The hold of the last job's line, from its last byte to its end.
struct BqHold {
  bool     open;
  uint32_t from;        // when the last byte went out
  uint32_t last_look;   // for BQ_POLL_MS
  uint32_t heard;       // bytes the printer sent meanwhile
  char     host[16];    // an IPv4 address as typed, for the log
};
static BqHold s_hold = {};

// What the printer said during the hold, until the screen takes it.
static BrotherQlReply s_reply = {};
static bool s_reply_new = false;

#define BQ_HOLD_CLOSED  "closed by printer"
#define BQ_HOLD_TIMEOUT "still open after 10 s, closed"
#define BQ_HOLD_NEXT    "closed for the next label"

BleSessionPhase brotherQlPhase() { return s_phase; }
void brotherQlBytes(size_t* sent, size_t* total) {
  if (sent)  *sent  = s_sent;
  if (total) *total = s_total;
}

bool brotherQlHolding() { return s_hold.open; }

bool brotherQlTakeReply(BrotherQlReply* out) {
  if (!s_reply_new) return false;
  s_reply_new = false;
  if (out) *out = s_reply;
  return true;
}

// What one run knows about itself, for the log line.
struct BqRun {
  size_t      total;   // the job's size, known once the raster has passed
  size_t      low;     // the least internal heap that was free at any check
  const char* line;    // what became of the line after the job
};

#define BQ_LINE_NONE "line not held"
#define BQ_LINE_HELD "line held, looked at from the loop"

static void noteHeap(BqRun& run) {
  const size_t now = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  if (now < run.low) run.low = now;
}

// Closes the held line and says how the hold ended.
static void holdEnd(const char* how) {
  s_line.stop();
  s_hold.open = false;
  logSDf("Printer: Brother %s line %s after %u ms, %u B from the printer",
         s_hold.host, how, (unsigned)(millis() - s_hold.from), (unsigned)s_hold.heard);
}

// Hands bytes to the socket and counts what it took. A short write is a
// failure: the line is gone or the printer stopped reading, and a job cannot
// be taken up again from the middle.
static bool putBytes(WiFiClient& client, const uint8_t* data, size_t len) {
  const size_t n = client.write(data, len);
  s_sent += n;
  return n == len;
}

// The job, in the order the printer wants it (brother_ql_rules.h): the null
// bytes, the start bytes, one line per raster row, the print byte. Each row is
// mirrored into the one small buffer and written at once.
static bool sendJob(WiFiClient& client, const BrotherQlLabel& label, const LabelRaster& image,
                    BleProgressFn progress, BqRun& run) {
  uint8_t buf[BROTHER_QL_LINE_BYTES];

  for (size_t i = 0; i < BROTHER_QL_INVALIDATE / sizeof(NULL_BLOCK); i++)
    if (!putBytes(client, NULL_BLOCK, sizeof(NULL_BLOCK))) return false;

  const size_t head = brotherQlStart(label, image.height, buf, sizeof(buf));
  if (head != BROTHER_QL_START_BYTES || !putBytes(client, buf, head)) return false;

  for (uint16_t y = 0; y < image.height; y++) {
    brotherQlRow(image.pixels + size_t(y) * image.row_bytes, buf);
    if (!putBytes(client, buf, BROTHER_QL_LINE_BYTES)) return false;
    noteHeap(run);
    if (progress) progress();
  }

  const uint8_t print = BROTHER_QL_PRINT;
  return putBytes(client, &print, 1);
}

static BrotherQlResult runJob(const char* host, const BrotherQlLabel& label,
                              const LabelRaster& image, BleProgressFn progress, BqRun& run) {
  if (!labelRasterValid(image) || image.row_bytes != BROTHER_QL_ROW_BYTES ||
      image.height != label.dots_h)
    return BQ_BAD_RASTER;
  run.total = brotherQlJobBytes(image.height);

  // The label before has been with the printer for a while by now: its line
  // is closed and this one prints at once, rather than waiting out the rest
  // of the hold (Joerg, 26.10.2026). Two lines to one printer are not tried.
  if (s_hold.open) holdEnd(BQ_HOLD_NEXT);
  s_reply_new = false;

  if (WiFi.status() != WL_CONNECTED) return BQ_NO_WIFI;

  crumbSet("brother connect");
  s_phase = BLE_PHASE_CONNECT;
  if (progress) progress();
  // The address as a person types it and nothing else: IPAddress::fromString
  // would also take an IPv6 address, and the socket is opened by address.
  IPAddress ip;
  if (!brotherQlHostValid(host) || !ip.fromString(host)) return BQ_CONNECT;
  if (!s_line.connect(ip, (uint16_t)BROTHER_QL_PORT, BQ_CONNECT_MS)) return BQ_CONNECT;
  noteHeap(run);

  crumbSet("brother send");
  s_phase = BLE_PHASE_SEND;
  s_total = run.total;
  if (progress) progress();
  if (!sendJob(s_line, label, image, progress, run)) {
    s_line.stop();
    return BQ_WRITE;
  }

  // All bytes are with the printer, or on their way in the socket. The line
  // stays open for it, up to BROTHER_QL_HOLD_MS, and brotherQlTick() looks
  // at it from the loop (brother_ql.h: the hold).
  s_hold.open      = true;
  s_hold.from      = millis();
  s_hold.last_look = s_hold.from;
  s_hold.heard     = 0;
  strncpy(s_hold.host, host, sizeof(s_hold.host) - 1);
  s_hold.host[sizeof(s_hold.host) - 1] = '\0';
  run.line = BQ_LINE_HELD;
  return BQ_SENT;
}

void brotherQlTick() {
  if (!s_hold.open) return;
  const uint32_t now = millis();
  if (now - s_hold.last_look < BQ_POLL_MS) return;
  s_hold.last_look = now;

  // One recv() that does not wait, on the client's own socket: it tells the
  // printer's close (0) from a reset (-1, ECONNRESET), which connected() does
  // not - its peek reads a close as "still up" (NetworkClient.cpp, core
  // 3.3.12). Nothing else reads this socket during the hold. A socket that
  // is gone counts as a broken line.
  uint8_t buf[BROTHER_QL_REPLY_MAX];
  const int fd = s_line.fd();
  int got = -1, err = EBADF;
  if (fd >= 0) {
    errno = 0;
    got = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
    err = errno;
  }
  const bool would_block = got < 0 && (err == EWOULDBLOCK || err == EAGAIN);
  const uint32_t held = now - s_hold.from;

  switch (brotherQlHoldVerdict(got, would_block, held)) {
    case BQH_WAIT:
      return;
    case BQH_DATA:
      // The first chunk goes to the screen, every one into the count.
      if (s_hold.heard == 0) {
        s_reply.kind = BQR_BYTES;
        s_reply.n    = (uint8_t)got;
        s_reply.err  = 0;
        memcpy(s_reply.bytes, buf, (size_t)got);
        s_reply_new  = true;
        char hex[BROTHER_QL_REPLY_MAX * 3];
        brotherQlBytesHex(buf, (size_t)got, hex, sizeof(hex));
        logSDf("Printer: Brother %s sent %d B after %u ms: %s", s_hold.host, got,
               (unsigned)held, hex);
      }
      s_hold.heard += (uint32_t)got;
      // A printer that keeps sending does not keep the line past the limit.
      if (held >= BROTHER_QL_HOLD_MS) holdEnd(BQ_HOLD_TIMEOUT);
      return;
    case BQH_CLOSED:
      holdEnd(BQ_HOLD_CLOSED);
      return;
    case BQH_TIMEOUT:
      holdEnd(BQ_HOLD_TIMEOUT);
      return;
    case BQH_ABORT: {
      s_reply.kind = BQR_BROKEN;
      s_reply.n    = 0;
      s_reply.err  = err;
      s_reply_new  = true;
      char how[40];
      snprintf(how, sizeof(how), "broken (errno %d)", err);
      holdEnd(how);
      return;
    }
  }
}

static const char* resultName(BrotherQlResult r) {
  switch (r) {
    case BQ_SENT:       return "sent";
    case BQ_BAD_RASTER: return "bad raster";
    case BQ_NO_WIFI:    return "no wifi";
    case BQ_CONNECT:    return "no connect";
    case BQ_WRITE:      return "write failed";
  }
  return "?";
}

BrotherQlResult brotherQlPrint(const char* host, const BrotherQlLabel& label,
                               const LabelRaster& image, BleProgressFn progress) {
  const uint32_t started = millis();
  s_sent = s_total = 0;
  BqRun run{0, heap_caps_get_free_size(MALLOC_CAP_INTERNAL), BQ_LINE_NONE};

  const BrotherQlResult r = runJob(host, label, image, progress, run);

  s_phase = BLE_PHASE_IDLE;
  crumbSet("loop");
  logSDf("Printer: Brother %s %s %u/%u B in %u ms, %s, internal low %u B",
         host ? host : "(none)", resultName(r), (unsigned)s_sent, (unsigned)run.total,
         (unsigned)(millis() - started), run.line, (unsigned)run.low);
  return r;
}
