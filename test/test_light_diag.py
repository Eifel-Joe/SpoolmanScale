"""Diagnosis of the 7B light (n61, acceptance firmware only, reverted after the cause is found): compile the rules that judge a readback of the CH32V003 and write its frames into the log, and check them against cases spelled out here."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]

source = r'''
#include "boards/waveshare_s3_lcd7b/light_diag_rules.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdint.h>

static void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAILED: %s\n", what);
    std::exit(1);
  }
}

using namespace light_diag;

int main() {
  // What was sent: the last value of each register, refused or not, since the
  // driver keeps its byte either way.
  Sent s;
  expect(!s.has_out && !s.has_pwm, "nothing sent yet");
  note(s, 0x05, 0xF5);
  note(s, 0x03, 0x44);
  note(s, 0x02, 0xFF);          // the direction is not a light register
  expect(s.has_pwm && s.pwm == 0xF5, "the PWM sent is kept");
  expect(s.has_out && s.out == 0x44, "the outputs sent are kept");
  note(s, 0x05, 0x05);
  expect(s.pwm == 0x05, "a later PWM replaces the earlier");

  // The readback: register 0x05 against the PWM sent, 0x03 against the
  // outputs, the input register's backlight bit against the backlight bit of
  // the outputs. EXIO2 is the backlight.
  const uint8_t EN = 2;
  Readback rb = {true, true, true, 0x44, 0x44, 0x05};
  expect(verdict(s, rb, EN) == V_OK, "everything arrived");
  rb.r05 = 0xF5;
  expect(verdict(s, rb, EN) == V_PWM, "the PWM still holds the dim value: lost");
  rb.r05 = 0x05;
  rb.r03 = 0x40;
  expect(verdict(s, rb, EN) == V_OUT, "the outputs hold another byte");
  rb.r03 = 0x44;
  rb.r04 = 0x40;
  expect(verdict(s, rb, EN) == V_EN_PIN, "the backlight pin reads low although it was switched on");
  rb.r04 = 0x44;
  rb.ok05 = false;
  expect(verdict(s, rb, EN) == V_READ, "a read the chip refused is no finding about the PWM");
  rb.ok05 = true;
  Sent none;
  expect(verdict(none, rb, EN) == V_OK, "nothing sent, nothing to compare");

  char text[24];
  verdictText(text, sizeof(text), V_OK);
  expect(std::strcmp(text, "ok") == 0, "ok spelled");
  verdictText(text, sizeof(text), V_PWM | V_EN_PIN);
  expect(std::strcmp(text, "PWM,EN") == 0, "two findings, comma separated");
  verdictText(text, sizeof(text), V_PWM | V_OUT | V_EN_PIN | V_READ);
  expect(std::strcmp(text, "PWM,OUT,EN,READ") == 0, "all four");

  // The frames of one wake, with the time from each to the one before it in
  // microseconds, and what endTransmission() said (0 acknowledged).
  const Frame frames[] = {
    {1000000u, 0x05, 0x05, 0},
    {1000148u, 0x03, 0x44, 0},
    {1000300u, 0x03, 0x44, 2},
  };
  char line[128];
  formatFrames(line, sizeof(line), frames, 3);
  expect(std::strcmp(line, "W05=05/0 +148us W03=44/0 +152us W03=44/2") == 0, line);
  // The microsecond clock is kept as 32 bits and wraps after 71 minutes.
  const Frame wrap[] = {{0xFFFFFF00u, 0x05, 0xF5, 0}, {0x00000064u, 0x03, 0x40, 0}};
  formatFrames(line, sizeof(line), wrap, 2);
  expect(std::strcmp(line, "W05=F5/0 +356us W03=40/0") == 0, line);
  // Too small a buffer cuts the text, never writes past it.
  char tiny[12];
  std::memset(tiny, 'x', sizeof(tiny));
  formatFrames(tiny, 10, frames, 3);
  expect(std::strlen(tiny) == 9 && tiny[11] == 'x', "the text stops at the buffer");
  formatFrames(line, sizeof(line), frames, 0);
  expect(line[0] == '\0', "no frames, no text");

  std::puts("light diagnosis rules: ok");
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
