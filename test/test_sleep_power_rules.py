"""Compile the production rules for saving power on the 7B (services/sleep_power_rules.cpp, Spec 5.3 AD, V81 and V82) and check them: what is switched off before the deep sleep and in which order (the reader by its reset line when the probe at the start found it working, otherwise by the PN532's sleep command; the weighing; a log line; the RS485 line held high and the SD card deselected last, since nothing may write after it), that the display's converter and reset are not in the list in either form (V82: switched off, the panel lies half powered and shows a halo after the wake), the wake rule (the touch chip's status byte at 0x814E says whether a touch is there; without one the scale sleeps on, a second wake within 30 s and a chip that does not answer start it), the reader rule on the battery (on from 15 g either way, off 10 s after the weight left, on while the scale waits for a tag; always on where it does not apply) and the NAU7802's power-down bits."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/sleep_power_rules.h"
#include <cassert>
#include <cstdint>

using namespace sleeppwr;

// The values, spelled out here on purpose: the production values must agree
// with this text and with Spec 5.3 AD, not with themselves.
static_assert(SECOND_WAKE_S == 30, "a second wake within 30 s starts the scale");
static_assert(GT_STATUS_READY == 0x80, "GT911 0x814E bit 7: a report is ready");
static_assert(LOAD_G == 15.0f, "the reader is on from 15 g");
static_assert(PARK_AFTER_MS == 10000, "and off 10 s after the weight left");

static bool contains(const Cuts& c, Step s) {
  for (uint8_t i = 0; i < c.n; i++) if (c.step[i] == s) return true;
  return false;
}
static int indexOf(const Cuts& c, Step s) {
  for (uint8_t i = 0; i < c.n; i++) if (c.step[i] == s) return i;
  return -1;
}

// One tick of the reader rule where it applies, without a tag being waited for.
static Act tick(Gate& g, float w, uint32_t now) { return readerGate(g, true, false, w, now); }

int main() {
  // --- What goes off before the deep sleep -------------------------------
  const Cuts line = sleepCuts(true);
  const Cuts cmd  = sleepCuts(false);
  // The reader goes off one way only: by its reset line when the probe found
  // it working, otherwise by the sleep command.
  assert(contains(line, STEP_READER_LINE) && !contains(line, STEP_READER_COMMAND));
  assert(contains(cmd, STEP_READER_COMMAND) && !contains(cmd, STEP_READER_LINE));
  const Cuts* both[] = {&line, &cmd};
  for (const Cuts* c : both) {
    assert(c->n == 5);
    // Reader, weighing, log line, RS485 high, SD deselected: in this order.
    assert(indexOf(*c, STEP_READER_LINE) == 0 || indexOf(*c, STEP_READER_COMMAND) == 0);
    assert(indexOf(*c, STEP_SCALE) == 1);
    assert(indexOf(*c, STEP_LOG) == 2);
    assert(indexOf(*c, STEP_RS485_HIGH) == 3);
    // Nothing writes after the card is deselected: it comes last.
    assert(indexOf(*c, STEP_SD_DESELECT) == (int)c->n - 1);
    // V82: the display's converter and its reset stay as they are. Every
    // step is one of the five above, none touches the panel.
    for (uint8_t i = 0; i < c->n; i++) {
      const Step s = c->step[i];
      assert(s == STEP_READER_LINE || s == STEP_READER_COMMAND || s == STEP_SCALE ||
             s == STEP_LOG || s == STEP_RS485_HIGH || s == STEP_SD_DESELECT);
    }
  }

  // --- The wake rule ------------------------------------------------------
  // A report is ready (bit 7): a finger, start. Also with touch points in
  // the low bits.
  assert(wakeRule(0x80, 1000, 0, false) == WAKE_START);
  assert(wakeRule(0x81, 1000, 0, false) == WAKE_START);
  // The chip does not answer: start, a finger may be there.
  assert(wakeRule(-1, 1000, 0, false) == WAKE_START);
  // No report: sleep on, the first time.
  assert(wakeRule(0x00, 1000, 0, false) == WAKE_SLEEP_ON);
  assert(wakeRule(0x05, 1000, 0, false) == WAKE_SLEEP_ON);   // points, but no report
  // A second wake without a report within 30 s: start.
  assert(wakeRule(0x00, 1000, 990, true) == WAKE_START);
  assert(wakeRule(0x00, 1030, 1000, true) == WAKE_START);
  // After 31 s it is a new first one: sleep on.
  assert(wakeRule(0x00, 1031, 1000, true) == WAKE_SLEEP_ON);
  // A clock that went backwards cannot say how long ago: start.
  assert(wakeRule(0x00, 500, 1000, true) == WAKE_START);

  // --- The reader on the battery -----------------------------------------
  // Where the rule does not apply (on the mains, no battery, no weighing, no
  // reader, the supply not yet known) the reader is on, and one that was off
  // is switched on.
  {
    Gate g;
    assert(g.on);
    assert(readerGate(g, false, false, 0.0f, 0) == ACT_NONE && g.on);
    for (uint32_t t = 0; t <= 60000; t += 500) assert(readerGate(g, false, false, 0.0f, t) == ACT_NONE);
    assert(g.on);
  }
  // Empty scale: off 10 s after the rule began to apply, not before.
  {
    Gate g;
    assert(tick(g, 0.0f, 1000) == ACT_NONE && g.on);
    assert(tick(g, 0.0f, 10999) == ACT_NONE && g.on);
    assert(tick(g, 0.0f, 11000) == ACT_OFF && !g.on);
    assert(tick(g, 0.0f, 12000) == ACT_NONE && !g.on);       // off once, not again
    // Weight from 15 g: on at once.
    assert(tick(g, 14.9f, 13000) == ACT_NONE && !g.on);
    assert(tick(g, 15.0f, 13500) == ACT_ON && g.on);
    assert(tick(g, 800.0f, 14000) == ACT_NONE && g.on);
    // The weight leaves: off 10 s later. Weight coming back stops the count,
    // and it starts anew when the weight leaves again.
    assert(tick(g, 3.0f, 20000) == ACT_NONE && g.on);
    assert(tick(g, 800.0f, 25000) == ACT_NONE && g.on);
    assert(tick(g, 3.0f, 26000) == ACT_NONE && g.on);
    assert(tick(g, 3.0f, 35999) == ACT_NONE && g.on);
    assert(tick(g, 3.0f, 36000) == ACT_OFF && !g.on);
    // -15 g and less counts as weight: taken off what lay there at the tare.
    assert(tick(g, -14.9f, 37000) == ACT_NONE && !g.on);
    assert(tick(g, -15.0f, 37500) == ACT_ON && g.on);
  }
  // Back on the mains with the reader off: on at once.
  {
    Gate g;
    tick(g, 0.0f, 0);
    assert(tick(g, 0.0f, 10000) == ACT_OFF && !g.on);
    assert(readerGate(g, false, false, 0.0f, 10500) == ACT_ON && g.on);
    // On the battery again: a fresh 10 s.
    assert(tick(g, 0.0f, 11000) == ACT_NONE && g.on);
    assert(tick(g, 0.0f, 20999) == ACT_NONE && g.on);
    assert(tick(g, 0.0f, 21000) == ACT_OFF && !g.on);
  }
  // Waiting for a tag (write, find by tag; E1 a): on without weight, and the
  // 10 s start when the wait ends.
  {
    Gate g;
    tick(g, 0.0f, 0);
    assert(tick(g, 0.0f, 10000) == ACT_OFF && !g.on);
    assert(readerGate(g, true, true, 0.0f, 11000) == ACT_ON && g.on);
    assert(readerGate(g, true, true, 0.0f, 40000) == ACT_NONE && g.on);
    assert(tick(g, 0.0f, 41000) == ACT_NONE && g.on);
    assert(tick(g, 0.0f, 50999) == ACT_NONE && g.on);
    assert(tick(g, 0.0f, 51000) == ACT_OFF && !g.on);
  }
  // The milliseconds wrap after 49 days: the 10 s still hold.
  {
    Gate g;
    const uint32_t t0 = 0xFFFFF000u;
    assert(tick(g, 0.0f, t0) == ACT_NONE);
    assert(tick(g, 0.0f, t0 + 9999u) == ACT_NONE && g.on);
    assert(tick(g, 0.0f, t0 + 10000u) == ACT_OFF && !g.on);
  }

  // --- NAU7802 power-down -------------------------------------------------
  // PU_CTRL (0x00): PUD is bit 1, PUA bit 2; both cleared, the rest kept.
  assert(nauPowerDown(0xFF) == 0xF9);
  assert(nauPowerDown(0x06) == 0x00);
  assert(nauPowerDown(0xAE) == 0xA8);
  assert(nauPowerDown(0x00) == 0x00);
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "sleep_power_rules.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("sleep power rules: ok")
