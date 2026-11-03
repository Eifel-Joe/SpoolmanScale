"""Compile the production battery rules of the 7B (services/battery_rules.cpp, Spec 5.3 AB, V60) and check them against what was measured on the board on 03.11.2026 (meta repo docs/recherche/2026-11-03-beta20/messung.md): the conversion of the CH32V003's ADC reading, how a ten-second window of readings tells no battery, charging and running on the battery apart, that the supply only changes after two windows that agree, that the level shown in per cent never rises on the battery and never falls while charging, that each warning comes once per discharge, when an update is refused, and where the display sits in the header line."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "services/battery_rules.h"
#include <cassert>
#include <cstdint>

using namespace bat;

// The thresholds, spelled out here on purpose: the production values must
// agree with this text and with the measurement, not with themselves.
static_assert(READ_MS == 250, "a reading every 250 ms");
static_assert(WINDOW_READS == 40, "a window is ten seconds");
static_assert(CONFIRM == 2, "two windows that agree change the supply");
static_assert(NO_BATTERY_MV == 2500, "below 2.5 V nothing is at the input");
static_assert(SPAN_QUIET == 15 && SPAN_BUSY == 20, "charging 7..11, on the battery 24..28");
static_assert(SPAN_PULSING == 40 && PULSE_MIN_MV == 4000, "no battery at UART: 47..63 around 4.34 V");
static_assert(LOW_PCT == 20 && UPDATE_MIN_PCT == 50, "warning at 20 %, update from 50 % (Jorg)");
// The runtime measurement on Jorg's 7B (03.11.2026, 2000 mAh, full
// brightness, 149.75 minutes from full until the board went dark): the
// minute's mean under load at these shares of the runtime left. "Almost
// empty" at 10 % (Jorg), which came 17 minutes before the end.
static_assert(CRITICAL_MV == 3390 && CRITICAL_PCT == 10, "almost empty at 10 %: 3390 mV");
static_assert(JUMP_MV == 60, "a supply at UART lifts the reading by 180 mV, the threshold a third of it");

// A window of 40 good readings: half at mean - span/2, half at mean + span/2.
static Window win(uint16_t mean_raw, uint16_t span) {
  Window w;
  windowClear(w);
  for (int i = 0; i < 40; i++) windowAdd(w, (uint16_t)(i % 2 ? mean_raw + span / 2 : mean_raw - span / 2));
  return w;
}

// The raw reading for a voltage, the other way round.
static uint16_t rawOf(uint32_t mv) { return (uint16_t)((mv * 1023u + 4950u) / 9900u); }

// Feeds n windows and returns the events of the last one.
static uint8_t feedN(State& s, const Window& w, int n) {
  uint8_t ev = 0;
  for (int i = 0; i < n; i++) ev = feed(s, w);
  return ev;
}

int main() {
  // 10 bit, 3.3 V reference, VBAT through 20 k / 10 k: Waveshare's demo.
  assert(mvOf(0) == 0);
  assert(mvOf(1023) == 9900);
  assert(mvOf(440) == 4258);
  assert(mvOf(394) == 3813);

  // One window each, as measured.
  assert(classify(win(1, 2), UNKNOWN) == NONE);              // no battery, supply at "USB": 7 mV
  assert(classify(win(448, 55), UNKNOWN) == NONE);           // no battery at "UART": 4.34 V, jumping
  assert(classify(win(415, 9), UNKNOWN) == CHARGING);        // charging: 4.01 V, quiet
  assert(classify(win(395, 26), UNKNOWN) == BATTERY);        // on the battery: 3.82 V, the boost's ripple
  assert(classify(win(400, 22), UNKNOWN) == BATTERY);        // "USB" with a battery: not charging
  // Between quiet and busy the window says what is already known.
  assert(classify(win(400, 17), CHARGING) == CHARGING);
  assert(classify(win(400, 17), BATTERY) == BATTERY);
  assert(classify(win(400, 17), UNKNOWN) == UNKNOWN);
  // A battery near empty may ripple more, but at its voltage the charger
  // never pulses: a wide span below 4 V is still the battery.
  assert(classify(win(rawOf(3450), 45), UNKNOWN) == BATTERY);
  // A window with fewer than 30 good readings says nothing.
  Window few;
  windowClear(few);
  for (int i = 0; i < 20; i++) windowAdd(few, 395);
  assert(classify(few, BATTERY) == UNKNOWN);

  // The supply changes after two windows that agree, not after one.
  State s;
  assert(s.supply == UNKNOWN && s.shown < 0);
  assert(feed(s, win(395, 26)) == 0 && s.supply == UNKNOWN);
  uint8_t ev = feed(s, win(395, 26));
  assert(s.supply == BATTERY && (ev & EV_SUPPLY) && (ev & EV_STEP) && s.shown >= 0 && s.shown <= 100);
  // One window of the unplug jump does not switch, nor does a single one
  // that looks like the charger pulsing.
  feed(s, win(448, 55));
  assert(s.supply == BATTERY);
  feed(s, win(415, 9));
  assert(s.supply == BATTERY);
  ev = feed(s, win(415, 9));
  assert(s.supply == CHARGING && (ev & EV_SUPPLY));

  // Near empty the boost's ripple shrinks (spans 17..18 in the last minutes,
  // 03.11.2026): a quiet window on the battery is charging only with the
  // lift a supply brings (+180 mV measured), not by its quiet alone.
  State e;
  feedN(e, win(rawOf(3100), 26), 2);
  assert(e.supply == BATTERY);
  feedN(e, win(rawOf(3090), 12), 4);
  assert(e.supply == BATTERY);
  feedN(e, win(rawOf(3300), 9), 2);
  assert(e.supply == CHARGING);

  // On the battery the shown level never rises: less load (the light dims,
  // the scale wakes up) lifts the voltage, not the charge.
  State b;
  feedN(b, win(rawOf(3900), 26), 2);
  const int8_t first = b.shown;
  feedN(b, win(rawOf(3700), 26), 6);
  const int8_t lower = b.shown;
  assert(lower < first);
  feedN(b, win(rawOf(3900), 26), 6);
  assert(b.shown == lower);
  // While charging it never falls.
  State c;
  feedN(c, win(rawOf(4100), 9), 2);
  const int8_t charging = c.shown;
  feedN(c, win(rawOf(3950), 9), 6);
  assert(c.shown == charging);
  // A window that disagrees with the supply (the jump itself) is not part
  // of the mean: the minute after it is.
  State j;
  feedN(j, win(rawOf(3820), 26), 8);
  const uint32_t before = j.mv;
  feed(j, win(rawOf(4000), 9));
  assert(j.supply == BATTERY && j.mv == before);

  // Single per cents (Jorg, 03.11.2026, Spec V63): the minute's mean is
  // steady to a few millivolts on one board, and on the battery the level
  // never rises, so a fine step does not jump back and forth. The tables are
  // monotonic and end at 0 and 100.
  static_assert(STEP_PCT == 1, "single per cents");
  assert(stepOf(0) == 0 && stepOf(4) == 4 && stepOf(5) == 5 && stepOf(64) == 64 && stepOf(100) == 100);
  // The log keeps its line per step of ten (Spec 5.3 AB): a new decade, or
  // the first level at all, is worth a line; 64 after 65 is not.
  assert(logWorthy(-1, 100) && logWorthy(100, 99) && logWorthy(70, 69) && logWorthy(61, 59));
  assert(!logWorthy(99, 95) && !logWorthy(65, 64) && !logWorthy(60, 60) && !logWorthy(9, 1));
  assert(logWorthy(9, 10) && logWorthy(1, 0) == false && logWorthy(10, 9));
  uint8_t last = 100;
  for (uint32_t mv = 2500; mv <= 4400; mv += 5) {
    const uint8_t p = pctOnBattery(mv);
    assert(p <= 100);
    if (mv > 2500) assert(p >= last);
    last = p;
  }
  assert(pctOnBattery(2600) == 0 && pctOnBattery(4300) == 100);
  assert(pctOnBattery(4098) == 100 && pctOnBattery(3850) == 80 && pctOnBattery(3591) == 50);
  assert(pctOnBattery(3458) == 20 && pctOnBattery(3390) == 10 && pctOnBattery(3096) == 0);
  last = 100;
  for (uint32_t mv = 2500; mv <= 4400; mv += 5) {
    const uint8_t p = pctCharging(mv);
    assert(p <= 100);
    if (mv > 2500) assert(p >= last);
    last = p;
  }
  assert(pctCharging(2600) == 0 && pctCharging(4400) == 100);
  assert(iconOf(100) == ICON_FULL && iconOf(90) == ICON_FULL && iconOf(80) == ICON_3 &&
         iconOf(50) == ICON_2 && iconOf(30) == ICON_1 && iconOf(10) == ICON_EMPTY && iconOf(0) == ICON_EMPTY);

  // Each warning once per discharge, again after the battery was charged.
  State w;
  feedN(w, win(rawOf(3900), 26), 2);
  bool low = false, crit = false;
  for (uint32_t mv = 3900; mv + 100 >= CRITICAL_MV; mv -= 10) {
    const uint8_t e = feedN(w, win(rawOf(mv), 26), 1);
    if (e & EV_LOW) { assert(!low); low = true; assert(w.shown <= LOW_PCT); }
    if (e & EV_CRITICAL) { assert(!crit); crit = true; assert(w.mv <= CRITICAL_MV); }
  }
  assert(low && crit);
  assert(toneOf(w) == TONE_CRITICAL);

  // The colours (Jorg, 03.11.2026, Spec V62): green from 50 %, yellow from
  // 20 % to under 50 %, red below 20 % and below the warning voltage; the
  // bolt green while charging.
  static_assert(GOOD_PCT == 50, "green from 50 %");
  State t;
  t.supply = BATTERY;
  t.hist_n = 1;
  t.mv = CRITICAL_MV + 300;
  const int8_t steps[] = {100, 50, 40, 20, 10, 0};
  const Tone tones[] = {TONE_GOOD, TONE_GOOD, TONE_LOW, TONE_LOW, TONE_CRITICAL, TONE_CRITICAL};
  for (int i = 0; i < 6; i++) {
    t.shown = steps[i];
    assert(toneOf(t) == tones[i]);
  }
  t.shown = 60;
  t.mv = CRITICAL_MV;
  assert(toneOf(t) == TONE_CRITICAL);
  feedN(w, win(rawOf(3600), 9), 2);
  assert(w.supply == CHARGING && toneOf(w) == TONE_CHARGING);
  low = crit = false;
  for (int i = 0; i < 10; i++) {
    const uint8_t e = feed(w, win(rawOf(CRITICAL_MV - 50), 26));
    if (e & EV_LOW) { assert(!low); low = true; }
    if (e & EV_CRITICAL) { assert(!crit); crit = true; }
  }
  assert(w.supply == BATTERY && low && crit);

  // The update: free without a battery, before the supply is known, while
  // charging and from 50 % on; refused on the battery below 50 % and below
  // the warning voltage, also while charging.
  State u;
  assert(!updateBlocked(u));                                  // not known yet
  feedN(u, win(1, 2), 2);
  assert(u.supply == NONE && !updateBlocked(u) && toneOf(u) == TONE_HIDDEN);
  // The voltages either side of 50 % come from the table itself, so that a
  // table measured anew does not break the rule's test.
  uint32_t below50 = 0, from50 = 0;
  for (uint32_t mv = 3000; mv <= 4300; mv += 5) {
    const uint8_t step = stepOf(pctOnBattery(mv));
    if (step < UPDATE_MIN_PCT && mv > CRITICAL_MV + 100) below50 = mv;
    if (step >= UPDATE_MIN_PCT && from50 == 0) from50 = mv;
  }
  assert(below50 > 0 && from50 > 0);
  State v;
  feedN(v, win(rawOf(below50 - 30), 26), 2);   // 30 mV inside: a raw count is 9.7 mV
  assert(v.supply == BATTERY && v.shown < UPDATE_MIN_PCT && updateBlocked(v));
  State x;
  feedN(x, win(rawOf(from50 + 30), 26), 2);
  assert(x.supply == BATTERY && x.shown >= UPDATE_MIN_PCT && !updateBlocked(x));
  State y;
  feedN(y, win(rawOf(below50 - 30), 9), 2);
  assert(y.supply == CHARGING && !updateBlocked(y));
  State z;
  feedN(z, win(rawOf(CRITICAL_MV - 100), 9), 2);
  assert(z.supply == CHARGING && z.mv <= CRITICAL_MV && updateBlocked(z));

  // The header line, 480 wide: the version ends at left_end, the first chip
  // begins at right_start. Centred on the screen where it fits, otherwise
  // centred in the gap, 6 px from both; symbol and number, the symbol alone,
  // or nothing.
  Slot r = headerSlot(191, 283, 49, 15);                      // release, no SD
  assert(r.form == FORM_FULL && r.x == 240 - 49 / 2);
  r = headerSlot(191, 264, 49, 15);                           // release with the SD symbol
  assert(r.form == FORM_FULL && r.x >= 191 + 6 && r.x + 49 <= 264 - 6);
  r = headerSlot(200, 264, 49, 15);                           // acceptance "A" with the SD symbol
  assert(r.form == FORM_FULL && r.x >= 206 && r.x + 49 <= 258);
  r = headerSlot(255, 283, 49, 15);                           // "-abnahme", no SD
  assert(r.form == FORM_SYMBOL && r.x >= 261 && r.x + 15 <= 277);
  r = headerSlot(255, 264, 49, 15);                           // "-abnahme" with the SD symbol
  assert(r.form == FORM_NONE);
  return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), str(root / "src" / "services" / "battery_rules.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("battery rules: ok")
