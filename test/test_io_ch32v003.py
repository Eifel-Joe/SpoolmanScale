"""Compile the production 7B expander driver against a Wire that records every frame, and check what reaches the chip: levels before direction, GPIO19/20 always on USB, a pin write the chip refuses sent again and counted, two frames never closer than 2 ms, and the battery reading of register 0x06, low byte first, in one transaction."""
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]

# The Arduino core's clock, as far as the driver uses it: micros() reads a
# clock of the test's own, delayMicroseconds() moves it on.
arduino_h = r'''
#pragma once
#include <stdint.h>
extern uint32_t g_now_us;
inline unsigned long micros() { return g_now_us; }
inline void delayMicroseconds(uint32_t us) { g_now_us += us; }
'''

# The Arduino core's TwoWire, as far as the driver uses it (the same
# signatures as libraries/Wire/src/Wire.h of arduino-esp32 3.3), recording
# each transmission instead of sending it, with the time it began and ended:
# a frame of three bytes at 400 kHz lasts about 72 us.
wire_h = r'''
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <deque>
#include <vector>
#include "Arduino.h"

struct Frame {
  uint8_t address;
  std::vector<uint8_t> bytes;
  uint32_t start_us, end_us;
  bool stop = true;   // false: a repeated start follows, the read of the same transaction
  bool read = false;  // a read: bytes holds what came back
};

class TwoWire {
 public:
  std::vector<Frame> frames;
  // What the next transmissions return, one each, in turn; 0, acknowledged,
  // once they are used up. arduino-esp32 3.3 returns 2 for a NACK, 5 for a
  // timeout and 4 for anything else (endTransmission() in Wire.cpp).
  std::deque<uint8_t> answers;

  void beginTransmission(uint8_t address) { open_ = Frame{address, {}, g_now_us, 0}; }
  size_t write(uint8_t value) {
    open_.bytes.push_back(value);
    return 1;
  }
  uint8_t endTransmission() { return endTransmission(true); }
  uint8_t endTransmission(bool sendStop) {
    g_now_us += 72;
    open_.end_us = g_now_us;
    open_.stop = sendStop;
    frames.push_back(open_);
    if (answers.empty()) return 0;
    const uint8_t answer = answers.front();
    answers.pop_front();
    return answer;
  }

  // What the chip hands back to the next reads, byte by byte; a read asks
  // for more than there is gets what there is.
  std::deque<uint8_t> replies;
  uint8_t requestFrom(uint8_t address, uint8_t quantity) {
    Frame f{address, {}, g_now_us, 0};
    f.read = true;
    while (f.bytes.size() < quantity && !replies.empty()) {
      f.bytes.push_back(replies.front());
      replies.pop_front();
    }
    g_now_us += 25u * (1u + quantity);
    f.end_us = g_now_us;
    frames.push_back(f);
    rx_ = f.bytes;
    rx_at_ = 0;
    return (uint8_t)f.bytes.size();
  }
  int read() { return rx_at_ < rx_.size() ? rx_[rx_at_++] : -1; }

 private:
  Frame open_{0, {}, 0, 0};
  std::vector<uint8_t> rx_;
  size_t rx_at_ = 0;
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

uint32_t g_now_us = 0;

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

  // Two frames to the chip never closer than 2 ms, from the end of the one
  // to the start of the next. The chip acknowledges a frame at once and
  // takes it over later: on the 7B a frame followed within 0.8 ms by the
  // next was lost or mixed with it, from 0.9 ms on none in 280 tries (n61,
  // 02.11.2026). The wake from "screen off" sends the brightness and then
  // the switch-on 0.13 ms apart and lost the brightness 10 times in 10.
  static constexpr uint32_t SPACING_US = 2000;
  auto spaced = [&](size_t from) {
    for (size_t i = from + 1; i < bus.frames.size(); i++) {
      if ((uint32_t)(bus.frames[i].start_us - bus.frames[i - 1].end_us) < SPACING_US) return false;
    }
    return true;
  };
  g_now_us += 50000;
  size_t from = bus.frames.size();
  expect(io_ch32v003::begin(bus, 0x00), "begin, long after the frame before");
  expect(spaced(from), "begin's three frames are 2 ms apart");
  from = bus.frames.size() - 1;
  expect(io_ch32v003::pwm(0x03), "the brightness right after begin");
  expect(io_ch32v003::write(2, true), "the switch-on right after the brightness");
  expect(io_ch32v003::pwm(0x03), "the brightness again right after the switch-on");
  expect(spaced(from), "begin, brightness, switch-on and brightness are 2 ms apart");
  from = bus.frames.size() - 1;
  bus.answers = {NACK, NACK};
  expect(io_ch32v003::write(6, true), "a write acknowledged at the third try");
  expect(spaced(from), "the tries of a refused write are 2 ms apart");
  // No wait for a frame long after the one before.
  g_now_us += 10000;
  const uint32_t asked_at = g_now_us;
  expect(io_ch32v003::pwm(0x05), "a frame 10 ms after the one before");
  expect(bus.frames.back().start_us == asked_at, "a frame 10 ms after the one before goes out at once");
  // micros() wraps after 71 minutes; the spacing holds across.
  g_now_us = 0xFFFFFE00u;
  expect(io_ch32v003::pwm(0x07), "a frame just before the clock wraps");
  from = bus.frames.size() - 1;
  expect(io_ch32v003::write(2, false), "a frame right after it");
  expect(spaced(from), "two frames on either side of the wrap are 2 ms apart");
  expect(bus.frames.back().start_us < 0x1000u, "the second frame went out after the wrap");

  // The scale's brightness, 0..255 with 255 full, as the register takes it
  // (Spec 5.3 S, V21): turned round, 0 is full on this chip, and never darker
  // than 247, 97 % of 255, where Waveshare's driver stops ("Prevent the
  // screen from completely turning off", io_extension.c).
  expect(io_ch32v003::PWM_DARKEST == 247, "the darkest register value is 97 % of 255");
  expect(io_ch32v003::pwmFor(255) == 0, "full brightness is register 0");
  expect(io_ch32v003::pwmFor(128) == 127, "half brightness is 127");
  expect(io_ch32v003::pwmFor(10) == 245, "the slider's least, 10, is 245");
  expect(io_ch32v003::pwmFor(8) == 247 && io_ch32v003::pwmFor(0) == 247, "darker than that stops at 247");
  uint8_t before = 0;
  for (int b = 255; b >= 0; b--) {
    const uint8_t r = io_ch32v003::pwmFor((uint8_t)b);
    expect(r >= before, "a lower brightness never gives a brighter register value");
    expect(r <= io_ch32v003::PWM_DARKEST, "no brightness goes past the darkest value");
    before = r;
  }
  const size_t lit_from = bus.frames.size();
  expect(io_ch32v003::brightness(128), "a brightness is acknowledged");
  expect(bus.frames.size() == lit_from + 1 && frameIs(bus.frames.back(), PWM, 127),
         "a brightness is one frame to register 0x05");
  bus.answers = {NACK};
  expect(!io_ch32v003::brightness(255), "a brightness the chip refuses fails");

  // The battery input (w05, Spec 5.3 AB): register 0x06 holds the ADC
  // reading of VBAT/3, two bytes, low first, as Waveshare's
  // DEV_I2C_Read_Word() puts them together. The register byte and the read
  // are one transaction (a repeated start), as Waveshare's driver reads
  // (i2c_master_transmit_receive); on the 7B this and a read with a stop in
  // between agreed in 2000 reads without a failure (03.11.2026). The
  // transaction keeps the 2 ms to the frames before and after it.
  static constexpr uint8_t ADC = 0x06;
  g_now_us += 50000;
  expect(io_ch32v003::pwm(0x09), "a frame before the reading");
  size_t at = bus.frames.size();
  bus.replies = {0x8C, 0x01};
  uint16_t raw = 0;
  expect(io_ch32v003::readAdc(raw), "a reading the chip answers");
  expect(raw == 0x018C, "the reading is the low byte and then the high byte");
  expect(bus.frames.size() == at + 2, "a reading is the register byte and the read");
  const Frame& reg = bus.frames[at];
  const Frame& rd = bus.frames[at + 1];
  expect(reg.address == ADDR && !reg.read && reg.bytes.size() == 1u && reg.bytes[0] == ADC,
         "the reading names register 0x06");
  expect(!reg.stop, "the register byte ends in a repeated start, not a stop");
  expect(rd.address == ADDR && rd.read && rd.bytes.size() == 2u, "the read asks for two bytes");
  expect((uint32_t)(reg.start_us - bus.frames[at - 1].end_us) >= SPACING_US,
         "a reading starts 2 ms after the frame before");
  expect(io_ch32v003::write(2, true), "a write right after the reading");
  expect((uint32_t)(bus.frames.back().start_us - rd.end_us) >= SPACING_US,
         "the frame after a reading starts 2 ms after its read");
  // Refused register byte: no read, false, and the write counters stay.
  retries_at = io_ch32v003::retries();
  failures_at = io_ch32v003::failures();
  at = bus.frames.size();
  bus.answers = {NACK};
  bus.replies = {0x8C, 0x01};
  expect(!io_ch32v003::readAdc(raw), "a reading whose register byte is refused fails");
  expect(bus.frames.size() == at + 1, "a refused register byte is not followed by a read");
  bus.replies.clear();
  // One byte instead of two: false.
  bus.replies = {0x8C};
  expect(!io_ch32v003::readAdc(raw), "a reading with one byte back fails");
  expect(io_ch32v003::retries() == retries_at && io_ch32v003::failures() == failures_at,
         "a reading leaves the write counters alone");

  // Whatever was sent, GPIO19/20 stayed on the USB connector.
  for (const Frame& f : bus.frames) {
    if (!f.read && f.bytes.size() == 2u && f.bytes[0] == OUTPUTS) {
      expect((f.bytes[1] & USB_SEL) == 0, "no frame to the outputs sets EXIO5");
    }
  }
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    (Path(tmp) / "Wire.h").write_text(wire_h)
    (Path(tmp) / "Arduino.h").write_text(arduino_h)
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", tmp, "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "boards" / "waveshare_s3_lcd7b" / "io_ch32v003.cpp"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

# The 7B dims the light itself, the 7 keeps switching it (Spec 5.3 S): the
# 7's CH422G has no PWM. On the 7B the brightness goes to the register, and
# the light comes on at the brightness, not at full and then down.
boards = root / "src" / "boards"
squeeze = lambda t: re.sub(r"\s+", "", t)


def body(text, name):
    m = re.search(r"\b" + name + r"\([^)]*\)\s*\{", text)
    assert m, f"{name}() not found"
    i, depth = m.end(), 1
    while depth:
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        i += 1
    return squeeze(text[m.end():i - 1])


assert "#defineBOARD_BACKLIGHT_PWM1" in squeeze((boards / "waveshare_s3_lcd7b" / "board_config.h").read_text()),     "the 7B does not dim its backlight (BOARD_BACKLIGHT_PWM 1)"
assert "#defineBOARD_BACKLIGHT_PWM0" in squeeze((boards / "waveshare_s3_lcd7" / "board_config.h").read_text()),     "the 7 has no PWM for its light and must keep BOARD_BACKLIGHT_PWM 0"
panel = (boards / "waveshare_s3_lcd7b" / "panel.cpp").read_text(encoding="utf-8")
assert "io_ch32v003::brightness(brightness)" in body(panel, "panelSetBrightness"),     "the 7B's panelSetBrightness() does not hand the brightness to the expander"
on = body(panel, "panelBacklightOn")
assert "panelSetBrightness(brightness);" in on and "io_ch32v003::write(hw_pins::EXIO_BACKLIGHT,true)" in on,     "the 7B's panelBacklightOn() does not set the brightness and switch the light on"
assert on.index("panelSetBrightness(") < on.index("io_ch32v003::write(hw_pins::EXIO_BACKLIGHT,true)"),     "the 7B's light comes on before its brightness is set"
print("CH32V003 expander: ok")
