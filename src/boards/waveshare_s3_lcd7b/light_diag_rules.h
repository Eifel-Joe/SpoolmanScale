#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Diagnosis of the 7B light (n61), in the acceptance firmware only and
// reverted once the cause is found: the rules that judge what the CH32V003
// holds after a light command and that write its frames into the log.
// test/test_light_diag.py checks them on the host.
namespace light_diag {

// The last value sent to each light register, refused or not: the driver
// keeps its byte either way (io_ch32v003::write()).
struct Sent {
  bool    has_out = false, has_pwm = false;
  uint8_t out = 0, pwm = 0;
};

inline void note(Sent& s, uint8_t reg, uint8_t value) {
  if (reg == 0x03) { s.out = value; s.has_out = true; }
  if (reg == 0x05) { s.pwm = value; s.has_pwm = true; }
}

// Register 0x04 (the levels the eight pins read at, the read Waveshare's own
// driver does) and 0x05 (PWM), each with whether the chip answered. Not 0x03:
// whether a read of it makes the chip's firmware write something there is not
// known, and 0x04 shows what the pins do.
struct Readback {
  bool    ok04, ok05;
  uint8_t r04, r05;
};

enum : uint8_t { V_OK = 0, V_PWM = 1, V_OUT = 2, V_EN_PIN = 4, V_READ = 8 };

// Which does not hold what was sent: the PWM, any pin against the outputs
// byte, and the backlight pin on its own. A read the chip refused is a
// finding of its own and says nothing about the register it was for.
inline uint8_t verdict(const Sent& s, const Readback& rb, uint8_t en_bit) {
  uint8_t v = V_OK;
  if (!rb.ok04 || !rb.ok05) v |= V_READ;
  if (s.has_pwm && rb.ok05 && rb.r05 != s.pwm) v |= V_PWM;
  if (s.has_out && rb.ok04 && rb.r04 != s.out) v |= V_OUT;
  if (s.has_out && rb.ok04 &&
      ((rb.r04 >> en_bit) & 1u) != ((s.out >> en_bit) & 1u)) v |= V_EN_PIN;
  return v;
}

inline void verdictText(char* out, size_t cap, uint8_t v) {
  if (!cap) return;
  out[0] = '\0';
  if (v == V_OK) { snprintf(out, cap, "ok"); return; }
  static const char* const NAMES[] = {"PWM", "OUT", "EN", "READ"};
  size_t used = 0;
  for (uint8_t bit = 0; bit < 4; bit++) {
    if (!(v & (1u << bit))) continue;
    const int n = snprintf(out + used, cap - used, "%s%s", used ? "," : "", NAMES[bit]);
    if (n < 0 || (size_t)n >= cap - used) return;
    used += (size_t)n;
  }
}

// One frame to the chip: when (esp_timer, microseconds, 32 bits), register,
// value, and what endTransmission() said (0 acknowledged).
struct Frame {
  uint32_t t_us;
  uint8_t  reg, val, res;
};

// "W05=05/0 +148us W03=44/0": each frame, and between two the microseconds
// from the one to the next. Unsigned differences, so the clock may wrap.
inline void formatFrames(char* out, size_t cap, const Frame* f, size_t n) {
  if (!cap) return;
  out[0] = '\0';
  size_t used = 0;
  for (size_t i = 0; i < n; i++) {
    int k;
    if (i == 0) {
      k = snprintf(out + used, cap - used, "W%02X=%02X/%u", f[i].reg, f[i].val, f[i].res);
    } else {
      k = snprintf(out + used, cap - used, " +%luus W%02X=%02X/%u",
                   (unsigned long)(uint32_t)(f[i].t_us - f[i - 1].t_us),
                   f[i].reg, f[i].val, f[i].res);
    }
    if (k < 0 || (size_t)k >= cap - used) return;
    used += (size_t)k;
  }
}

}
