#include "io_ch32v003.h"

namespace io_ch32v003 {

static constexpr uint8_t ADDR          = 0x24;
static constexpr uint8_t REG_DIRECTION = 0x02;   // a bit set: that pin drives
static constexpr uint8_t REG_OUTPUTS   = 0x03;
static constexpr uint8_t REG_PWM       = 0x05;
static constexpr uint8_t ALL_OUTPUTS   = 0xFF;

static TwoWire* s_wire    = nullptr;
static uint8_t  s_outputs = 0;

static bool writeReg(uint8_t reg, uint8_t value) {
  if (!s_wire) return false;
  s_wire->beginTransmission(ADDR);
  s_wire->write(reg);
  s_wire->write(value);
  return s_wire->endTransmission() == 0;
}

// Direction first and the levels right after, the order of Waveshare's own
// driver. The other way round would be the CH422G's, but whether this
// firmware keeps a level written to a pin that is still an input is not
// known, and a level it dropped would leave the pin wherever it was.
bool begin(TwoWire& wire, uint8_t outputs) {
  s_wire    = &wire;
  s_outputs = outputs;
  const bool direction_ok = writeReg(REG_DIRECTION, ALL_OUTPUTS);
  const bool outputs_ok   = writeReg(REG_OUTPUTS, s_outputs);
  return direction_ok && outputs_ok;
}

bool write(uint8_t bit, bool on) {
  if (on) s_outputs |= (uint8_t)(1u << bit);
  else    s_outputs &= (uint8_t)~(1u << bit);
  return writeReg(REG_OUTPUTS, s_outputs);
}

bool pwm(uint8_t value) {
  return writeReg(REG_PWM, value);
}

}
