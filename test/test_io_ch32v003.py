"""Compile the production 7B expander driver against a Wire that records every frame, and check what reaches the chip: levels before direction, GPIO19/20 always on USB."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]

# The Arduino core's TwoWire, as far as the driver uses it (the same
# signatures as libraries/Wire/src/Wire.h of arduino-esp32 3.3), recording
# each transmission instead of sending it.
wire_h = r'''
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>

struct Frame {
  uint8_t address;
  std::vector<uint8_t> bytes;
};

class TwoWire {
 public:
  static constexpr size_t NONE = (size_t)-1;
  std::vector<Frame> frames;
  size_t nack_at = NONE;  // the frame, by index, the chip does not acknowledge

  void beginTransmission(uint8_t address) { open_ = Frame{address, {}}; }
  size_t write(uint8_t value) {
    open_.bytes.push_back(value);
    return 1;
  }
  uint8_t endTransmission() {
    frames.push_back(open_);
    return frames.size() - 1 == nack_at ? 2 : 0;  // 2: address not acknowledged
  }

 private:
  Frame open_{0, {}};
};
'''

source = r'''
#include "boards/waveshare_s3_lcd7b/io_ch32v003.h"
#include <cstdio>
#include <cstdlib>
#include <stdint.h>

// The chip and its registers are spelled out here on purpose: the driver
// must agree with this text, not with itself.
static constexpr uint8_t ADDR = 0x24, DIRECTION = 0x02, OUTPUTS = 0x03, PWM = 0x05;
static constexpr uint8_t USB_SEL = 1u << 5;  // EXIO5: high hands GPIO19/20 to CAN

static void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAILED: %s\n", what);
    std::exit(1);
  }
}

static bool frameIs(const Frame& f, uint8_t reg, uint8_t value) {
  return f.address == ADDR && f.bytes.size() == 2u && f.bytes[0] == reg && f.bytes[1] == value;
}

int main() {
  TwoWire bus;

  // Levels first, so that no pin drives whatever the chip held when it
  // becomes an output; then every pin an output; then the levels again, in
  // case the chip forgot the first ones while its pins were inputs.
  expect(io_ch32v003::begin(bus, 0x00), "begin is acknowledged");
  expect(bus.frames.size() == 3u, "begin sends three frames");
  expect(frameIs(bus.frames[0], OUTPUTS, 0x00), "begin sends the levels first");
  expect(frameIs(bus.frames[1], DIRECTION, 0xFF), "begin then makes every pin an output");
  expect(frameIs(bus.frames[2], OUTPUTS, 0x00), "begin sends the levels again last");

  // One pin at a time: the whole byte goes out, the other pins as they were.
  // Touch reset, backlight, LCD reset and panel power, on and off in turn.
  struct Step { uint8_t bit; bool on; };
  const Step steps[] = {
    {6, true}, {3, true}, {1, false}, {1, true}, {2, true}, {6, false},
    {3, false}, {6, true}, {3, true}, {2, false}, {1, false}, {2, true},
    {1, true},
  };
  uint8_t expected = 0x00;
  for (const Step& s : steps) {
    const size_t before = bus.frames.size();
    expect(io_ch32v003::write(s.bit, s.on), "a pin write is acknowledged");
    if (s.on) expected |= (uint8_t)(1u << s.bit);
    else      expected &= (uint8_t)~(1u << s.bit);
    expect(bus.frames.size() == before + 1, "a pin write is one frame");
    expect(frameIs(bus.frames.back(), OUTPUTS, expected), "a pin write keeps the other pins");
  }
  expect(expected == 0x4E, "the sequence ends with pins 1, 2, 3 and 6 on");

  // begin() starts the kept byte afresh.
  expect(io_ch32v003::begin(bus, 0x14), "begin with levels is acknowledged");
  expect(io_ch32v003::write(2, false), "a pin write after begin is acknowledged");
  expect(frameIs(bus.frames.back(), OUTPUTS, 0x10), "a pin write starts from the levels begin set");

  // The backlight PWM, a register of its own.
  const uint8_t levels[] = {0x00, 0x80, 0xFF};
  for (uint8_t v : levels) {
    expect(io_ch32v003::pwm(v), "pwm is acknowledged");
    expect(frameIs(bus.frames.back(), PWM, v), "pwm sends the value to register 0x05");
  }

  // Only the writes after the direction decide: the first levels may land on
  // inputs that drop them, the last ones are what the pins hold.
  size_t first = bus.frames.size();
  bus.nack_at = first;
  expect(io_ch32v003::begin(bus, 0x00), "begin does not depend on the first levels");
  first = bus.frames.size();
  bus.nack_at = first + 1;
  expect(!io_ch32v003::begin(bus, 0x00), "begin fails when the direction is not acknowledged");
  first = bus.frames.size();
  bus.nack_at = first + 2;
  expect(!io_ch32v003::begin(bus, 0x00), "begin fails when the last levels are not acknowledged");

  // Whatever was sent, GPIO19/20 stayed on the USB connector.
  for (const Frame& f : bus.frames) {
    if (f.bytes.size() == 2u && f.bytes[0] == OUTPUTS) {
      expect((f.bytes[1] & USB_SEL) == 0, "no frame to the outputs sets EXIO5");
    }
  }
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    (Path(tmp) / "Wire.h").write_text(wire_h)
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", tmp, "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "boards" / "waveshare_s3_lcd7b" / "io_ch32v003.cpp"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("CH32V003 expander: ok")
