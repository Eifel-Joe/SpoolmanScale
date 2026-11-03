#include "hardware/battery_diag.h"

#if defined(BATTERY_DIAG)

#include <Arduino.h>
#include <math.h>
#include <stdlib.h>

#include "hardware/display_power.h"
#include "hardware/sd_logger.h"
#include "io_ch32v003.h"
#include "lang.h"
#include "web/web_access.h"

// Measurement of the 7B battery input (w05, beta.20): never in a release,
// reverted once the thresholds are measured (draft
// docs/entwurf/2026-11-03-akku-7b.md of the meta repo, section 9).
//
// What no data sheet says and this measures:
// - whether register 0x06 gives plausible values on this board, and whether
//   a read in one transaction (repeated start) and one with a stop between
//   register byte and read agree;
// - what the input shows without a battery, at the "USB" and at the "UART"
//   connector (the CS8501 pulses its charger then, every 0.5 to 2 s);
// - the steps when the supply at "UART" comes and goes, and how much the
//   backlight moves the voltage;
// - the discharge under load, from full until the board goes dark.
//
// Conversion as Waveshare's demo has it (15_LVGL_SLIDER, lvgl_demo.c):
// 10 bit, 3.3 V reference, VBAT/3.

static constexpr uint8_t  REG_ADC   = 0x06;
static constexpr uint32_t SAMPLE_MS = 250;
static constexpr int      RING      = 240;   // one minute at SAMPLE_MS

struct Sample {
  uint32_t ms;
  uint16_t raw;
  uint8_t  ok;
  uint8_t  idle;   // the display dimmed or dark: less load on the battery
};

static Sample   s_ring[RING];
static int      s_head          = 0;
static int      s_count         = 0;
static uint32_t s_last_sample   = 0;
static uint32_t s_last_log      = 0;
static uint32_t s_log_every_ms  = 10000;
static uint32_t s_reads         = 0;
static uint32_t s_fails         = 0;

static uint32_t mvOf(float raw) {
  return (uint32_t)(raw * 3.0f * 3300.0f / 1023.0f + 0.5f);
}

struct Stats {
  int      n;
  float    mean;
  uint16_t lo;
  uint16_t hi;
  float    sd;
  int      idle;
};

// Over the samples of the last window_ms that were read.
static Stats statsOver(uint32_t window_ms) {
  Stats st = {0, 0.0f, 0xFFFF, 0, 0.0f, 0};
  const uint32_t now = millis();
  double sum = 0.0, sq = 0.0;
  for (int i = 0; i < s_count; i++) {
    const Sample& s = s_ring[(s_head - 1 - i + RING) % RING];
    if (now - s.ms > window_ms) break;
    if (!s.ok) continue;
    st.n++;
    sum += s.raw;
    sq  += (double)s.raw * s.raw;
    if (s.raw < st.lo) st.lo = s.raw;
    if (s.raw > st.hi) st.hi = s.raw;
    if (s.idle) st.idle++;
  }
  if (st.n > 0) {
    st.mean = (float)(sum / st.n);
    const double var = sq / st.n - (sum / st.n) * (sum / st.n);
    st.sd = var > 0.0 ? (float)sqrt(var) : 0.0f;
  } else {
    st.lo = 0;
  }
  return st;
}

void batteryDiagTick() {
  const uint32_t now = millis();
  if (now - s_last_sample < SAMPLE_MS) return;
  s_last_sample = now;

  uint16_t raw = 0;
  const bool ok = io_ch32v003::readWord(REG_ADC, raw);
  s_reads++;
  if (!ok) s_fails++;
  s_ring[s_head] = {now, raw, (uint8_t)(ok ? 1 : 0), (uint8_t)(displayIdleFor(0) ? 1 : 0)};
  s_head = (s_head + 1) % RING;
  if (s_count < RING) s_count++;

  if (s_log_every_ms > 0 && now - s_last_log >= s_log_every_ms) {
    s_last_log = now;
    const Stats st = statsOver(s_log_every_ms);
    logSDf("BD %u mV mean, %u..%u mV, raw %.1f sd %.1f, n %d, idle %d, fails %u/%u",
           (unsigned)mvOf(st.mean), (unsigned)mvOf(st.lo), (unsigned)mvOf(st.hi),
           st.mean, st.sd, st.n, st.idle, (unsigned)s_fails, (unsigned)s_reads);
  }
}

static String statsJson(const char* name, uint32_t window_ms) {
  const Stats st = statsOver(window_ms);
  String j = String("\"") + name + "\":{\"n\":" + st.n +
             ",\"raw_mean\":" + String(st.mean, 1) +
             ",\"raw_min\":" + st.lo + ",\"raw_max\":" + st.hi +
             ",\"raw_sd\":" + String(st.sd, 2) +
             ",\"mv_mean\":" + mvOf(st.mean) +
             ",\"mv_min\":" + mvOf(st.lo) + ",\"mv_max\":" + mvOf(st.hi) +
             ",\"idle\":" + st.idle + "}";
  return j;
}

// A whole number argument, def when missing, -1 when it is not a number.
static long argOr(WebServer& srv, const char* name, long def) {
  if (!srv.hasArg(name)) return def;
  const String v = srv.arg(name);
  char* end = nullptr;
  const long n = strtol(v.c_str(), &end, 10);
  if (v.length() == 0 || *end != '\0') return -1;
  return n;
}

void batteryDiagRoutes(WebServer& srv) {
  srv.on("/api/diag/battery", HTTP_GET, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_NAV_LOGS))) return;
    String j = String("{\"ms\":") + millis() + ",\"reads\":" + s_reads +
               ",\"fails\":" + s_fails + ",\"log_s\":" + (s_log_every_ms / 1000) +
               ",\"idle\":" + (displayIdleFor(0) ? 1 : 0) + ",";
    j += statsJson("s10", 10000) + ",";
    j += statsJson("s60", 60000) + ",\"last\":[";
    const int show = s_count < 40 ? s_count : 40;
    for (int i = show - 1; i >= 0; i--) {
      const Sample& s = s_ring[(s_head - 1 - i + RING) % RING];
      j += String("[") + s.ms + "," + s.raw + "," + s.ok + "," + s.idle + "]";
      if (i > 0) j += ",";
    }
    j += "]}";
    srv.send(200, "application/json", j);
  });

  srv.on("/api/diag/battery", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_NAV_LOGS))) return;
    const long s = argOr(srv, "log", 10);
    if (s < 0 || s > 600) {
      srv.send(400, "application/json", "{\"error\":\"log 0-600 s\"}");
      return;
    }
    s_log_every_ms = (uint32_t)s * 1000u;
    logSDf("BD log every %ld s", s);
    srv.send(200, "application/json", String("{\"log_s\":") + s + "}");
  });

  // A burst of readings with a chosen gap, read in one transaction (rs) or
  // with a stop between register byte and read (stop). At most two seconds.
  srv.on("/api/diag/battery/burst", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_NAV_LOGS))) return;
    const long n   = argOr(srv, "n", 20);
    const long gap = argOr(srv, "gap", 10);
    const String mode = srv.hasArg("mode") ? srv.arg("mode") : String("rs");
    if (n < 1 || n > 200 || gap < 2 || gap > 100 || n * gap > 2000 ||
        (mode != "rs" && mode != "stop")) {
      srv.send(400, "application/json", "{\"error\":\"n 1-200, gap 2-100 ms, n*gap <= 2000, mode rs|stop\"}");
      return;
    }
    const bool repeated = (mode == "rs");
    String j = String("{\"mode\":\"") + mode + "\",\"gap\":" + gap + ",\"raw\":[";
    uint32_t fails = 0;
    for (long i = 0; i < n; i++) {
      uint16_t raw = 0;
      const bool ok = io_ch32v003::readWord(REG_ADC, raw, repeated);
      if (!ok) fails++;
      j += ok ? String(raw) : String("null");
      if (i + 1 < n) j += ",";
      delay((uint32_t)gap);
    }
    j += String("],\"fails\":") + fails + "}";
    logSDf("BD burst %s n %ld gap %ld ms, fails %u", mode.c_str(), n, gap, (unsigned)fails);
    srv.send(200, "application/json", j);
  });
}

#endif  // BATTERY_DIAG
