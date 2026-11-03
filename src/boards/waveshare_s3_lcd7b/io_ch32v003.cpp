#include "io_ch32v003.h"

#include <Arduino.h>

namespace io_ch32v003 {

static constexpr uint8_t ADDR          = 0x24;
static constexpr uint8_t REG_DIRECTION = 0x02;   // a bit set: that pin drives
static constexpr uint8_t REG_OUTPUTS   = 0x03;
static constexpr uint8_t REG_PWM       = 0x05;
static constexpr uint8_t REG_ADC       = 0x06;
static constexpr uint8_t ALL_OUTPUTS   = 0xFF;

static TwoWire* s_wire     = nullptr;
static uint8_t  s_outputs  = 0;
static uint32_t s_retries  = 0;
static uint32_t s_failures = 0;

// The least time from the end of one frame to the start of the next.
//
// Root: the chip acknowledges a frame as its bytes arrive (its I2C hardware
// does that) and takes it over only later, in its own firmware. A frame the
// next one follows within 0.8 ms is lost, or the two are mixed into one
// write of the wrong value (the PWM value landed in the outputs register).
// Measured on the 7B with a diagnosis firmware that read the registers back
// (n61, 02.11.2026, docs/recherche/2026-11-02-beta18/ of the meta repo):
// from 0.9 ms on, none in 280 tries. The wake from "screen off" sent the
// brightness and the switch-on 0.13 ms apart and lost the brightness 10
// times in 10; the light came on at the dimmed level and stayed there, as a
// touch sends nothing once the display counts as awake.
// Fix: a frame waits until FRAME_GAP_US have passed since the last one
// ended, more than twice the longest gap that lost one. Only a burst waits
// (the boot, a wake from "screen off", the tries of a refused write); a
// single frame goes out at once.
// NOT-TO-DO: the brightness sent again after the switch-on (beta.17,
// a51e750) is a third frame as close, which lost the middle one, the
// switch-on, and the light stayed off. More tries do not help either: the
// chip acknowledged every lost frame. test/test_io_ch32v003.py checks the gap.
static constexpr uint32_t FRAME_GAP_US = 2000;
static uint32_t s_last_end_us = 0;
static bool     s_sent_once   = false;

static void waitForChip() {
  if (!s_sent_once) return;
  const uint32_t since = (uint32_t)micros() - s_last_end_us;
  if (since < FRAME_GAP_US) delayMicroseconds(FRAME_GAP_US - since);
}

static bool writeReg(uint8_t reg, uint8_t value) {
  if (!s_wire) return false;
  waitForChip();
  s_wire->beginTransmission(ADDR);
  s_wire->write(reg);
  s_wire->write(value);
  const bool ok = s_wire->endTransmission() == 0;
  s_last_end_us = (uint32_t)micros();
  s_sent_once   = true;
  return ok;
}

// The levels, then every pin an output, then the levels again.
//
// Not the direction first, the order of Waveshare's driver
// (IO_EXTENSION_Init() in io_extension.c) and of ESPHome's (setup() in
// waveshare_io_ch32v003.cpp). It leaves a window: a 1 the chip holds for
// EXIO5 from power-up drives that pin high from the direction frame until
// the levels frame, the switch U9 hands GPIO19/20 to the CAN transceiver,
// and USB drops on the cold boot whose serial log is the one that says what
// went wrong. While EXIO5 is still an input, R117 holds it low, on USB.
//
// The first levels close that window if the firmware keeps a level written
// to a pin that is an input. Whether it does is not known (its source is not
// published), so their acknowledgement decides nothing: if it drops them,
// this is the direction-first order with one frame more. The levels after
// the direction are the ones the pins hold, and only those two writes count.
// test/test_io_ch32v003.py checks the frames.
bool begin(TwoWire& wire, uint8_t outputs) {
  s_wire    = &wire;
  s_outputs = outputs;
  (void)writeReg(REG_OUTPUTS, s_outputs);
  const bool direction_ok = writeReg(REG_DIRECTION, ALL_OUTPUTS);
  const bool outputs_ok   = writeReg(REG_OUTPUTS, s_outputs);
  return direction_ok && outputs_ok;
}

// The same byte again, up to WRITE_ATTEMPTS frames in all, until the chip
// acknowledges one; any result of endTransmission() but 0 counts as refused
// (2 a NACK, 5 a timeout, 4 anything else: Wire.cpp of arduino-esp32 3.3).
//
// After the OTA update to 912c3ff (24.10.2026) the backlight stayed off over
// a frame buffer with the right picture and "Board: CH32V003 expander ready"
// in the boot log; a power cycle brought it back. A refused write was the
// guess then (H1); the counters, which panelBegin() logs, never showed one.
// What fits is FRAME_GAP_US above: begin() sent its frames 0.13 ms apart,
// and a direction mixed with a level leaves the pins inputs until the chip
// restarts, which an ESP restart does not do. That is not shown.
//
// The kept byte is not set back when every try fails: the next write of any
// pin then carries the level, which is what a lost backlight-on needs. The
// tries keep the chip's gap like every frame (writeReg()).
bool write(uint8_t bit, bool on) {
  if (on) s_outputs |= (uint8_t)(1u << bit);
  else    s_outputs &= (uint8_t)~(1u << bit);
  if (writeReg(REG_OUTPUTS, s_outputs)) return true;
  for (uint8_t again = 1; again < WRITE_ATTEMPTS; again++) {
    s_retries++;
    if (writeReg(REG_OUTPUTS, s_outputs)) return true;
  }
  s_failures++;
  return false;
}

uint32_t retries()  { return s_retries; }
uint32_t failures() { return s_failures; }

bool pwm(uint8_t value) {
  return writeReg(REG_PWM, value);
}

uint8_t pwmFor(uint8_t brightness) {
  const uint8_t value = (uint8_t)(255 - brightness);
  return value > PWM_DARKEST ? PWM_DARKEST : value;
}

bool brightness(uint8_t brightness) {
  return pwm(pwmFor(brightness));
}

bool readAdc(uint16_t& raw) {
  if (!s_wire) return false;
  waitForChip();
  s_wire->beginTransmission(ADDR);
  s_wire->write(REG_ADC);
  bool ok = s_wire->endTransmission(false) == 0 &&
            s_wire->requestFrom(ADDR, (uint8_t)2) == 2;
  if (ok) {
    const uint8_t lo = (uint8_t)s_wire->read();
    const uint8_t hi = (uint8_t)s_wire->read();
    raw = (uint16_t)(lo | ((uint16_t)hi << 8));
  }
  s_last_end_us = (uint32_t)micros();
  s_sent_once   = true;
  return ok;
}

}
