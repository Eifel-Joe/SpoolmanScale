#include "io_ch32v003.h"

namespace io_ch32v003 {

static constexpr uint8_t ADDR          = 0x24;
static constexpr uint8_t REG_DIRECTION = 0x02;   // a bit set: that pin drives
static constexpr uint8_t REG_OUTPUTS   = 0x03;
static constexpr uint8_t REG_PWM       = 0x05;
static constexpr uint8_t ALL_OUTPUTS   = 0xFF;

static TwoWire* s_wire     = nullptr;
static uint8_t  s_outputs  = 0;
static uint32_t s_retries  = 0;
static uint32_t s_failures = 0;

static bool writeReg(uint8_t reg, uint8_t value) {
  if (!s_wire) return false;
  s_wire->beginTransmission(ADDR);
  s_wire->write(reg);
  s_wire->write(value);
  return s_wire->endTransmission() == 0;
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
// in the boot log; a power cycle brought it back. panel.cpp did not look at
// what this returned, so a write the chip did not take would have gone
// unnoticed. That is a guess (H1), not a finding; the counters, which
// panelBegin() logs, are the evidence the next update brings.
//
// The kept byte is not set back when every try fails: the next write of any
// pin then carries the level, which is what a lost backlight-on needs. No
// pause between the tries, as this file includes Wire.h alone for the host
// test (test/test_io_ch32v003.py); should writes fail after every try, a
// pause is the next step, not more tries.
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

}
