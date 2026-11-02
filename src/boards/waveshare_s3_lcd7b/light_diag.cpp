#include "hardware/light_diag.h"

#if defined(LIGHT_DIAG)

#include <Arduino.h>
#include <Wire.h>
#include <esp_timer.h>
#include <stdlib.h>

#include "boards/board.h"
#include "hardware/display_power.h"
#include "hardware/sd_logger.h"
#include "io_ch32v003.h"
#include "lang.h"
#include "light_diag_rules.h"
#include "web/web_access.h"

// Diagnosis of the 7B light (n61): never in a release, reverted once the cause
// is found. Since beta.16 the light sometimes stays at the dimmed level after
// "screen off" and a touch, and a second brightness frame after the switch-on
// (beta.17, a51e750) left it dark although the chip acknowledged every frame.
// What reached the chip, as opposed to what it acknowledged, is what this
// records:
//
// - every frame io_ch32v003 sends, with its time in microseconds, kept in RAM
//   while the frames of one light command follow each other (nothing is
//   logged in between, a log line would stretch the gaps being measured);
// - 30 ms after the last frame of such a batch, two registers read back:
//   0x04, the levels of the eight pins (the read Waveshare's own driver does,
//   IO_EXTENSION_Input()), then 0x05, the PWM, which Waveshare never reads;
//   the batch, the event that sent it and the readback go into one log line;
// - one second later the same two again, in case the chip acts late.
//
// Never 0x02 or 0x03: the chip's firmware is not published, and should a read
// make it write the register with whatever byte it last received, a read of
// 0x03 could switch off the panel's power. A read of 0x05 risks no more than
// a wrong brightness, and the second read of 0x05 in every round of a test
// sequence shows whether it does ("LD probe" at boot shows what 0x04 and 0x05
// read at all). EXIO0 and EXIO7
// go nowhere on the board (sch-7b-U10-io-expander.png, befunde.md of
// 24.10.2026, section 4), so the test sequences toggle EXIO7 as a marker for
// the outputs frame and read it back in register 0x04.

using namespace light_diag;

static constexpr uint8_t  ADDR          = 0x24;
static constexpr uint8_t  MARKER_BIT    = 7;
static constexpr size_t   MAX_FRAMES    = 16;
static constexpr uint32_t SETTLE_US     = 30000;
static constexpr uint32_t LATE_US       = 1000000;

static Frame       s_frames[MAX_FRAMES];
static size_t      s_n          = 0;
static uint32_t    s_dropped    = 0;
static const char* s_event      = nullptr;
static Sent        s_sent;
static uint32_t    s_batch      = 0;
static uint32_t    s_mismatch   = 0;
static bool        s_quiet      = false;   // a test sequence logs itself
static bool        s_readback   = true;
static bool        s_late_due   = false;
static uint32_t    s_last_us    = 0;
static uint32_t    s_prev_us    = 0;       // last frame of the batch before
static bool        s_probed     = false;

static void hook(uint8_t reg, uint8_t value, uint8_t result) {
  note(s_sent, reg, value);
  if (s_quiet) return;
  const uint32_t now = (uint32_t)esp_timer_get_time();
  if (s_n < MAX_FRAMES) s_frames[s_n++] = {now, reg, value, result};
  else s_dropped++;
  s_last_us  = now;
  s_late_due = false;
}

// Before setup(): the frames of the boot are in the first batch.
static struct HookInstall {
  HookInstall() { io_ch32v003::setFrameHook(hook); }
} s_install;

void lightDiagEvent(const char* what) {
  s_event = what;
}

// The register pointer, a repeated start and one byte, as Waveshare reads the
// inputs (IO_EXTENSION_Input(), io_extension.c).
static bool readReg(uint8_t reg, uint8_t& out) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(ADDR, (uint8_t)1) != 1) return false;
  out = (uint8_t)Wire.read();
  return true;
}

static Readback readBack() {
  Readback rb = {false, false, 0, 0};
  rb.ok04 = readReg(0x04, rb.r04);
  rb.ok05 = readReg(0x05, rb.r05);
  return rb;
}

// "04=46 05=05 (sent 03=46 05=05)", "--" for a read the chip refused.
static void rbText(char* out, size_t cap, const Readback& rb) {
  char a[4] = "--", c[4] = "--";
  if (rb.ok04) snprintf(a, sizeof(a), "%02X", rb.r04);
  if (rb.ok05) snprintf(c, sizeof(c), "%02X", rb.r05);
  snprintf(out, cap, "04=%s 05=%s (sent 03=%02X 05=%02X)", a, c, s_sent.out, s_sent.pwm);
}

// Once, at the first pass of the main loop: what 0x04 and 0x05 read, twice
// each, against what the boot sent.
static void probe() {
  const Readback a = readBack();
  const Readback b = readBack();
  char ta[64], tb[64];
  rbText(ta, sizeof(ta), a);
  rbText(tb, sizeof(tb), b);
  logSDf("LD probe: %s, again %s", ta, tb);
}

void lightDiagTick() {
  if (!s_probed) {
    s_probed = true;
    // The batch of the boot first, so that the probe below reads after it.
    if (s_n) {
      char frames[200];
      formatFrames(frames, sizeof(frames), s_frames, s_n);
      logSDf("LD boot: %s", frames);
      s_prev_us = s_frames[s_n - 1].t_us;
      s_n = 0;
    }
    probe();
    return;
  }
  const uint32_t now = (uint32_t)esp_timer_get_time();
  if (s_n && (uint32_t)(now - s_last_us) >= SETTLE_US) {
    s_batch++;
    char frames[200];
    formatFrames(frames, sizeof(frames), s_frames, s_n);
    const uint32_t since_prev_ms = (uint32_t)(s_frames[0].t_us - s_prev_us) / 1000u;
    s_prev_us = s_frames[s_n - 1].t_us;
    char rbt[64] = "not read";
    char vt[24] = "-";
    if (s_readback) {
      const Readback rb = readBack();
      const uint8_t v = verdict(s_sent, rb, hw_pins::EXIO_BACKLIGHT);
      if (v != V_OK) s_mismatch++;
      rbText(rbt, sizeof(rbt), rb);
      verdictText(vt, sizeof(vt), v);
      s_late_due = true;
    }
    logSDf("LD #%lu %s (prev %lums): %s | %s %s%s", (unsigned long)s_batch,
           s_event ? s_event : "-", (unsigned long)since_prev_ms, frames, rbt, vt,
           s_dropped ? " FRAMES DROPPED" : "");
    s_event   = nullptr;
    s_n       = 0;
    s_dropped = 0;
    return;
  }
  if (s_late_due && !s_n && (uint32_t)(now - s_last_us) >= LATE_US) {
    s_late_due = false;
    const Readback rb = readBack();
    const uint8_t v = verdict(s_sent, rb, hw_pins::EXIO_BACKLIGHT);
    char rbt[64], vt[24];
    rbText(rbt, sizeof(rbt), rb);
    verdictText(vt, sizeof(vt), v);
    logSDf("LD #%lu +1s: %s %s", (unsigned long)s_batch, rbt, vt);
  }
}

// ---- web -----------------------------------------------------------------

static long longArg(WebServer& srv, const char* name, long def, long lo, long hi) {
  if (!srv.hasArg(name)) return def;
  const String v = srv.arg(name);
  char* end = nullptr;
  long n = strtol(v.c_str(), &end, 10);
  if (v.length() == 0 || *end != '\0') return def;
  if (n < lo) n = lo;
  if (n > hi) n = hi;
  return n;
}

static void busyWaitUs(uint32_t us) {
  if (!us) return;
  const int64_t until = esp_timer_get_time() + us;
  while (esp_timer_get_time() < until) {}
}

// One test sequence, n times, the light switch (EXIO2) never touched:
//   pe   PWM v, gap, outputs with the marker toggled  (the order of a wake from off)
//   pep  PWM v, gap, outputs with the marker toggled, gap, PWM v  (beta.17's try)
//   ep   outputs with the marker toggled, gap, PWM v
//   pp   PWM w, gap, PWM v  (w the other value)
//   p    PWM v alone
//   e    outputs with the marker toggled alone
// v alternates between a and b from one round to the next, so a PWM frame
// that did not arrive leaves the value of the round before; the marker
// alternates the same way. settle ms after each round 0x04 is read, and 0x05
// twice: a second value unlike the first means the first read changed the
// register. Afterwards the PWM and the marker go back to what they were.
struct SeqAsk {
  String   mode;
  uint32_t gap_us;
  int      n;
  uint32_t settle_ms;
  uint8_t  a, b;
};

static String runSeq(const SeqAsk& ask) {
  const String&  mode      = ask.mode;
  const uint32_t gap_us    = ask.gap_us;
  const int      n         = ask.n;
  const uint32_t settle_ms = ask.settle_ms;
  const uint8_t  a = ask.a, b = ask.b;
  const uint8_t pwm_before = s_sent.has_pwm ? s_sent.pwm : 0;
  const bool    mark_before = (s_sent.out >> MARKER_BIT) & 1u;
  s_quiet = true;
  int pwm_lost = 0, out_lost = 0, read_fail = 0, read_moved = 0;
  String rows = "[";
  uint8_t prev_v = pwm_before;
  bool    mark = mark_before;
  for (int i = 0; i < n; i++) {
    const uint8_t v = (i % 2 == 0) ? a : b;
    const uint8_t w = (v == a) ? b : a;
    const bool uses_pwm = mode != "e";
    const bool uses_out = mode == "pe" || mode == "pep" || mode == "ep" || mode == "e";
    const bool want_mark = uses_out ? !mark : mark;
    const int64_t t0 = esp_timer_get_time();
    int64_t t1 = t0, t2 = t0;
    bool ack1 = true, ack2 = true, ack3 = true;
    if (mode == "pe") {
      ack1 = io_ch32v003::pwm(v); t1 = esp_timer_get_time(); busyWaitUs(gap_us);
      ack2 = io_ch32v003::write(MARKER_BIT, want_mark); t2 = esp_timer_get_time();
    } else if (mode == "pep") {
      ack1 = io_ch32v003::pwm(v); busyWaitUs(gap_us);
      ack2 = io_ch32v003::write(MARKER_BIT, want_mark); t1 = esp_timer_get_time(); busyWaitUs(gap_us);
      ack3 = io_ch32v003::pwm(v); t2 = esp_timer_get_time();
    } else if (mode == "ep") {
      ack1 = io_ch32v003::write(MARKER_BIT, want_mark); t1 = esp_timer_get_time(); busyWaitUs(gap_us);
      ack2 = io_ch32v003::pwm(v); t2 = esp_timer_get_time();
    } else if (mode == "pp") {
      ack1 = io_ch32v003::pwm(w); t1 = esp_timer_get_time(); busyWaitUs(gap_us);
      ack2 = io_ch32v003::pwm(v); t2 = esp_timer_get_time();
    } else if (mode == "e") {
      ack1 = io_ch32v003::write(MARKER_BIT, want_mark); t1 = esp_timer_get_time();
    } else {
      ack1 = io_ch32v003::pwm(v); t1 = esp_timer_get_time();
    }
    delay(settle_ms);
    uint8_t r04 = 0, r05 = 0, r05b = 0;
    const bool ok04 = readReg(0x04, r04);
    const bool ok05 = readReg(0x05, r05);
    const bool ok05b = readReg(0x05, r05b);
    if (!ok04 || !ok05 || !ok05b) read_fail++;
    if (ok05 && ok05b && r05 != r05b) read_moved++;
    const bool got_mark = (r04 >> MARKER_BIT) & 1u;
    const bool pwm_ok = !uses_pwm || (ok05 && r05 == v);
    const bool out_ok = !uses_out || (ok04 && got_mark == want_mark);
    if (!pwm_ok) pwm_lost++;
    if (!out_ok) out_lost++;
    if (i) rows += ",";
    rows += "{\"i\":" + String(i) + ",\"v\":" + String(v) + ",\"prev\":" + String(prev_v) +
            ",\"r05\":" + String(r05) + ",\"r05b\":" + String(r05b) +
            ",\"mark\":" + String(want_mark ? 1 : 0) +
            ",\"r04\":" + String(r04) + ",\"pwm_ok\":" + String(pwm_ok ? 1 : 0) +
            ",\"out_ok\":" + String(out_ok ? 1 : 0) + ",\"ack\":\"" + String(ack1 ? 1 : 0) +
            String(ack2 ? 1 : 0) + String(ack3 ? 1 : 0) + "\",\"us\":[" +
            String((long)(t1 - t0)) + "," + String((long)(t2 - t0)) + "]}";
    prev_v = uses_pwm ? v : prev_v;
    mark = want_mark;
  }
  rows += "]";
  // Back to the brightness the display power set, the marker as it was.
  delay(settle_ms);
  io_ch32v003::write(MARKER_BIT, mark_before);
  delay(settle_ms);
  io_ch32v003::pwm(pwm_before);
  delay(settle_ms);
  uint8_t r05 = 0, r04 = 0;
  const bool ok05 = readReg(0x05, r05);
  const bool ok04 = readReg(0x04, r04);
  s_quiet = false;
  logSDf("LD seq %s gap=%luus n=%d settle=%lums a=%u b=%u: PWM lost %d, outputs lost %d, reads failed %d, "
         "read moved 05 %d; restored 05=%02X%s (want %02X) 04=%02X%s",
         mode.c_str(), (unsigned long)gap_us, n, (unsigned long)settle_ms, a, b,
         pwm_lost, out_lost, read_fail, read_moved, r05, ok05 ? "" : "!", pwm_before, r04, ok04 ? "" : "!");
  return String("{\"mode\":\"") + mode + "\",\"gap_us\":" + String((unsigned long)gap_us) +
         ",\"n\":" + String(n) + ",\"pwm_lost\":" + String(pwm_lost) + ",\"out_lost\":" +
         String(out_lost) + ",\"read_fail\":" + String(read_fail) + ",\"read_moved\":" +
         String(read_moved) + ",\"restored05\":" +
         String(r05) + ",\"want05\":" + String(pwm_before) + ",\"rows\":" + rows + "}";
}

void lightDiagRoutes(WebServer& srv) {
  srv.on("/api/diag/light", HTTP_GET, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_NAV_LOGS))) return;
    String j = "{\"sent03\":" + String(s_sent.out) + ",\"sent05\":" + String(s_sent.pwm) +
               ",\"dimmed\":" + String(displayDiagDimmed() ? 1 : 0) +
               ",\"off\":" + String(displayDiagOff() ? 1 : 0) +
               ",\"batches\":" + String((unsigned long)s_batch) +
               ",\"mismatch\":" + String((unsigned long)s_mismatch) +
               ",\"readback\":" + String(s_readback ? 1 : 0);
    // 0x04 and 0x05 only, see the head of this file.
    const Readback rb = readBack();
    j += ",\"r04\":" + (rb.ok04 ? String(rb.r04) : String("null")) +
         ",\"r05\":" + (rb.ok05 ? String(rb.r05) : String("null")) + "}";
    srv.send(200, "application/json", j);
  });

  srv.on("/api/diag/light", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_NAV_LOGS))) return;
    if (srv.hasArg("rb")) s_readback = longArg(srv, "rb", 1, 0, 1) != 0;
    const long age = longArg(srv, "age", 0, 0, 3600000);
    if (age > 0) {
      displayDiagAge((uint32_t)age);
      logSDf("LD: idle timer %ld ms older (web)", age);
    }
    srv.send(200, "application/json", String("{\"age\":") + age + ",\"readback\":" +
             String(s_readback ? 1 : 0) + "}");
  });

  srv.on("/api/diag/light/seq", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_NAV_LOGS))) return;
    const String mode = srv.hasArg("mode") ? srv.arg("mode") : String("pe");
    if (mode != "pe" && mode != "pep" && mode != "ep" && mode != "pp" && mode != "p" && mode != "e") {
      srv.send(400, "application/json", "{\"error\":\"mode pe, pep, ep, pp, p or e\"}");
      return;
    }
    SeqAsk ask;
    ask.mode      = mode;
    ask.gap_us    = (uint32_t)longArg(srv, "gap", 0, 0, 100000);
    ask.n         = (int)longArg(srv, "n", 10, 1, 40);
    ask.settle_ms = (uint32_t)longArg(srv, "settle", 50, 5, 500);
    ask.a         = (uint8_t)longArg(srv, "a", 5, 0, io_ch32v003::PWM_DARKEST);
    ask.b         = (uint8_t)longArg(srv, "b", 9, 0, io_ch32v003::PWM_DARKEST);
    srv.send(200, "application/json", runSeq(ask));
  });
}

#endif  // LIGHT_DIAG
