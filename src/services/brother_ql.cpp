#include "services/brother_ql.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <esp_heap_caps.h>

#include "hardware/sd_logger.h"
#include "services/breadcrumb.h"

// How long a connect may take. A printer that is off or on another network
// must be given up on quickly: the loop stands still meanwhile.
#define BQ_CONNECT_MS     3000
// How long the printer gets to close the line after the last byte. It does so
// once it has read the whole job; closing first leaves it busy (befunde 3).
// The log line below says "10 s" in words, so the two go together.
#define BQ_CLOSE_WAIT_MS  10000
static_assert(BQ_CLOSE_WAIT_MS == 10000, "the log text says 10 s");
// Pause between two looks at the line while waiting.
#define BQ_POLL_MS          50
// What the printer sends back, if anything, is read into this and dropped.
#define BQ_SINK_BYTES       32

// The null bytes in front of a job, from flash: a block of 100 written four
// times, so no buffer of 400 bytes ever sits in RAM.
static const uint8_t NULL_BLOCK[100] = {0};
static_assert(BROTHER_QL_INVALIDATE % sizeof(NULL_BLOCK) == 0,
              "the null bytes must be a whole number of blocks");
// One buffer serves for the 38 start bytes and for every row.
static_assert(BROTHER_QL_LINE_BYTES >= BROTHER_QL_START_BYTES,
              "the row buffer must hold the start bytes");

static volatile BleSessionPhase s_phase = BLE_PHASE_IDLE;
static size_t s_sent = 0, s_total = 0;

BleSessionPhase brotherQlPhase() { return s_phase; }
void brotherQlBytes(size_t* sent, size_t* total) {
  if (sent)  *sent  = s_sent;
  if (total) *total = s_total;
}

// What one run knows about itself, for the log line.
struct BqRun {
  size_t      total;   // the job's size, known once the raster has passed
  size_t      low;     // the least internal heap that was free at any check
  const char* wait;    // how the wait for the printer ended
};

#define BQ_WAIT_NONE   "not waited for"
#define BQ_WAIT_CLOSED "closed by printer"
#define BQ_WAIT_OPEN   "still open after 10 s"

static void noteHeap(BqRun& run) {
  const size_t now = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  if (now < run.low) run.low = now;
}

// Reads one chunk of what the printer has sent and drops it, once per round
// of the wait: draining until nothing was left let a peer that keeps sending
// hold the wait past its 10 s. Port 9100 answers nothing (befunde 3), so this
// is for what should not come. What is still unread keeps connected() true,
// so the wait sees the close once it is all read, or ends at its limit.
// read() is only called with data waiting: its first call makes the stack
// allocate a receive buffer of 1436 bytes in internal RAM, which a printer
// that stays silent never costs.
static void discardIncoming(WiFiClient& client) {
  uint8_t sink[BQ_SINK_BYTES];
  if (client.available() > 0) client.read(sink, sizeof(sink));
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

  if (WiFi.status() != WL_CONNECTED) return BQ_NO_WIFI;

  crumbSet("brother connect");
  s_phase = BLE_PHASE_CONNECT;
  if (progress) progress();
  // The address as a person types it and nothing else: IPAddress::fromString
  // would also take an IPv6 address, and the socket is opened by address.
  IPAddress ip;
  if (!brotherQlHostValid(host) || !ip.fromString(host)) return BQ_CONNECT;
  WiFiClient client;
  if (!client.connect(ip, (uint16_t)BROTHER_QL_PORT, BQ_CONNECT_MS)) return BQ_CONNECT;
  noteHeap(run);

  crumbSet("brother send");
  s_phase = BLE_PHASE_SEND;
  s_total = run.total;
  if (progress) progress();
  if (!sendJob(client, label, image, progress, run)) {
    client.stop();
    return BQ_WRITE;
  }

  // All bytes are with the printer. It closes the line when it has read them;
  // closing first leaves it busy. Past the limit the job is there all the
  // same, so the answer is the same: sent, and never again. connected() sees
  // the close one round late: its peek meets the printer's FIN first and
  // reads it as "still up", the next one gets ENOTCONN from lwIP (checked in
  // NetworkClient.cpp and lwIP api_lib.c of core 3.3.12, 26.10.2026).
  crumbSet("brother await");
  s_phase = BLE_PHASE_AWAIT;
  run.wait = BQ_WAIT_OPEN;
  const uint32_t from = millis();
  while (millis() - from < BQ_CLOSE_WAIT_MS) {
    discardIncoming(client);
    if (!client.connected()) {
      run.wait = BQ_WAIT_CLOSED;
      break;
    }
    noteHeap(run);
    if (progress) progress();
    delay(BQ_POLL_MS);
  }
  client.stop();
  return BQ_SENT;
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
  BqRun run{0, heap_caps_get_free_size(MALLOC_CAP_INTERNAL), BQ_WAIT_NONE};

  const BrotherQlResult r = runJob(host, label, image, progress, run);

  s_phase = BLE_PHASE_IDLE;
  crumbSet("loop");
  logSDf("Printer: Brother %s %s %u/%u B in %u ms, %s, internal low %u B",
         host ? host : "(none)", resultName(r), (unsigned)s_sent, (unsigned)run.total,
         (unsigned)(millis() - started), run.wait, (unsigned)run.low);
  return r;
}
