"""Compile the production 7B expander driver against a Wire that records every frame, and check what reaches the chip: levels before direction, GPIO19/20 always on USB, a pin write the chip refuses sent again and counted."""
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
#include <deque>
#include <vector>

struct Frame {
  uint8_t address;
  std::vector<uint8_t> bytes;
};

class TwoWire {
 public:
  std::vector<Frame> frames;
  // What the next transmissions return, one each, in turn; 0, acknowledged,
  // once they are used up. arduino-esp32 3.3 returns 2 for a NACK, 5 for a
  // timeout and 4 for anything else (endTransmission() in Wire.cpp).
  std::deque<uint8_t> answers;

  void beginTransmission(uint8_t address) { open_ = Frame{address, {}}; }
  size_t write(uint8_t value) {
    open_.bytes.push_back(value);
    return 1;
  }
  uint8_t endTransmission() {
    frames.push_back(open_);
    if (answers.empty()) return 0;
    const uint8_t answer = answers.front();
    answers.pop_front();
    return answer;
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
static constexpr uint8_t ACK = 0, NACK = 2, OTHER = 4, TIMEOUT = 5;  // endTransmission()

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
  // inputs that drop them, the last ones are what the pins hold. Neither
  // begin() nor pwm() sends a frame again, and neither counts for write().
  uint32_t retries_at = io_ch32v003::retries();
  uint32_t failures_at = io_ch32v003::failures();
  const size_t refused_from = bus.frames.size();
  bus.answers = {NACK};
  expect(io_ch32v003::begin(bus, 0x00), "begin does not depend on the first levels");
  bus.answers = {ACK, NACK};
  expect(!io_ch32v003::begin(bus, 0x00), "begin fails when the direction is not acknowledged");
  bus.answers = {ACK, ACK, NACK};
  expect(!io_ch32v003::begin(bus, 0x00), "begin fails when the last levels are not acknowledged");
  bus.answers = {NACK};
  expect(!io_ch32v003::pwm(0x00), "pwm fails when it is not acknowledged");
  expect(bus.frames.size() == refused_from + 3 * 3 + 1, "begin and pwm send each frame once, refused or not");
  expect(io_ch32v003::retries() == retries_at && io_ch32v003::failures() == failures_at,
         "begin and pwm leave the write counters alone");

  // A pin write the chip refuses goes out again, the same byte, three times
  // in all at most; the first try it acknowledges ends it.
  retries_at = io_ch32v003::retries();
  failures_at = io_ch32v003::failures();
  size_t first = bus.frames.size();
  bus.answers = {NACK};
  expect(io_ch32v003::write(2, true), "a write acknowledged at the second try succeeds");
  expect(bus.frames.size() == first + 2, "a write refused once is sent twice");
  expect(frameIs(bus.frames[first], OUTPUTS, 0x04) && frameIs(bus.frames[first + 1], OUTPUTS, 0x04),
         "the second try sends the same byte");
  expect(io_ch32v003::retries() - retries_at == 1u, "the second try counts as one retry");
  expect(io_ch32v003::failures() == failures_at, "a write that gets through at the second try is no failure");

  // Refused at every try, whatever the error: three frames, then false.
  retries_at = io_ch32v003::retries();
  failures_at = io_ch32v003::failures();
  first = bus.frames.size();
  bus.answers = {NACK, TIMEOUT, OTHER};
  expect(!io_ch32v003::write(6, true), "a write refused at every try fails");
  expect(bus.frames.size() == first + 3, "a write refused at every try is sent three times");
  for (size_t i = 0; i < 3; i++) {
    expect(frameIs(bus.frames[first + i], OUTPUTS, 0x44), "every try sends the same byte");
  }
  expect(io_ch32v003::retries() - retries_at == 2u, "the two tries after the first count as retries");
  expect(io_ch32v003::failures() - failures_at == 1u, "a write refused at every try counts as one failure");

  // The refused pin stays in the kept byte and goes out with the next write.
  expect(io_ch32v003::write(3, true), "the write after a failed one is acknowledged");
  expect(frameIs(bus.frames.back(), OUTPUTS, 0x4C), "the next write carries the pin that was refused");

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
