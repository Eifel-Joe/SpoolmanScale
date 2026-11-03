#pragma once

#include <stdint.h>

// ============================================================
//  THE BATTERY OF THE 7B: THE RULES
//
//  The 7B charges a LiPo through its CS8501 and reads the cell through a
//  20 k / 10 k divider on the ADC of its CH32V003 (register 0x06). Spec 5.3
//  AB (V60); draft docs/entwurf/2026-11-03-akku-7b.md of the meta repo,
//  measurement docs/recherche/2026-11-03-beta20/messung.md.
//
//  The board says nothing about its supply: the CS8501's charge and standby
//  outputs drive only two LEDs. What it does say, measured on Jorg's 7B on
//  03.11.2026, is in the readings themselves, four a second, judged ten
//  seconds at a time:
//
//    nothing at the input   0.007 V    no battery, supply at "USB" (the
//                                      charger has no input there)
//    jumping 4.1..4.7 V     span 47..63 raw: no battery, supply at "UART",
//                                      the charger pulsing into nothing
//    quiet, 4.01 V          span 7..11: charging, the boost is off
//    rippling, 3.82 V       span 24..28: on the battery, the boost runs;
//                                      also with the supply at "USB", which
//                                      does not charge (span 15..27)
//
//  The supply changes after CONFIRM windows that agree, so a single window
//  that holds the jump of a plug coming or going changes nothing.
//
//  The rules live here, without Arduino or the expander: a host test holds
//  them to the measurement (test_battery_rules.py), and services/battery.cpp,
//  which reads the chip and draws, asks this unit.
// ============================================================

namespace bat {

constexpr uint32_t READ_MS       = 250;   // one reading
constexpr uint16_t WINDOW_READS  = 40;    // a window: ten seconds
constexpr uint16_t WINDOW_MIN_OK = 30;    // fewer good readings: the window says nothing
constexpr uint8_t  AVG_WINDOWS   = 6;     // the level shown: the mean of the last minute
constexpr uint8_t  CONFIRM       = 2;     // windows that agree before the supply changes

// The measurement's margins (raw counts of about 9.7 mV, 10 bit).
constexpr uint32_t NO_BATTERY_MV = 2500;  // below: nothing at the input
constexpr uint16_t SPAN_QUIET    = 15;    // up to: the boost is off, the cell is charging
constexpr uint16_t SPAN_BUSY     = 20;    // from: the boost runs, the cell carries the board
constexpr uint16_t SPAN_PULSING  = 40;    // from, at PULSE_MIN_MV and above: the charger
constexpr uint32_t PULSE_MIN_MV  = 4000;  // pulsing with no cell behind it

constexpr uint8_t  GOOD_PCT       = 50;   // green from here (Jorg, Spec V62)
constexpr uint8_t  LOW_PCT        = 20;   // "battery low" once per discharge; red below
constexpr uint8_t  UPDATE_MIN_PCT = 50;   // Jorg: an update on the battery from 50 % on
// "Almost empty" at 10 % of the measured runtime (Jorg, 03.11.2026): the
// table's 3390 mV, which came 17 minutes before the board went dark at full
// brightness (the CS8501 stops its boost at 2.7 V, 2.9 V at worst; the last
// reading was 3.02 V). Below it an update is refused also while charging.
constexpr uint8_t  CRITICAL_PCT   = 10;
constexpr uint32_t CRITICAL_MV    = 3390;
// A supply at "UART" lifts the reading by about 180 mV (the load leaves the
// cell, the charge current comes in; measured 03.11.2026). Near empty the
// boost's ripple shrank to spans of 17..18 counts, so on the battery a quiet
// window counts as charging only with a third of that lift over the
// minute's mean.
constexpr uint32_t JUMP_MV        = 60;

// The CH32V003's ADC, as Waveshare's demo converts it: 10 bit, the 3.3 V
// supply as reference, VBAT/3 (15_LVGL_SLIDER, lvgl_demo.c).
uint32_t mvOf(uint32_t raw);

// Ten seconds of good readings.
struct Window {
  uint16_t n;
  uint32_t sum;
  uint16_t lo;
  uint16_t hi;
};
void windowClear(Window& w);
void windowAdd(Window& w, uint16_t raw);
uint32_t windowMv(const Window& w);   // the mean in millivolts

enum Supply : uint8_t {
  UNKNOWN = 0,   // not yet two windows that agree
  NONE,          // no battery fitted
  BATTERY,       // the cell carries the board
  CHARGING,      // a supply at "UART" carries the board and charges the cell
};

// What one window says. A window between quiet and busy says what is
// already known (now), and one with too few readings says UNKNOWN.
Supply classify(const Window& w, Supply now);

// Voltage to charge, 0..100: under the load of the running scale, and while
// charging. Measured on Jorg's 7B with a 2000 mAh lithium-ion battery at
// full brightness (Spec 5.3 AB, V63): the per cents are shares of the
// runtime left, so each lasts as long as the next.
uint8_t pctOnBattery(uint32_t mv);
uint8_t pctCharging(uint32_t mv);
// The step shown: single per cents (Jorg, 03.11.2026, Spec V63). The
// minute's mean is steady to a few millivolts on one board, and on the
// battery the level never rises, so a fine step does not jump. On another
// board or battery the reading may still be some 10 % off; the step does
// not change that.
constexpr uint8_t STEP_PCT = 1;
uint8_t stepOf(uint8_t pct);
// Whether a new level is worth a log line: the first one, and every change
// of the step of ten (Spec 5.3 AB) – not each of the hundred per cents.
bool logWorthy(int8_t logged, int8_t shown);

enum Icon : uint8_t { ICON_EMPTY = 0, ICON_1, ICON_2, ICON_3, ICON_FULL };
Icon iconOf(uint8_t shown);

struct State {
  Supply   supply     = UNKNOWN;
  Supply   cand       = UNKNOWN;   // what the last windows said
  uint8_t  cand_count = 0;
  uint32_t hist[AVG_WINDOWS] = {};
  uint8_t  hist_n     = 0;
  uint8_t  hist_head  = 0;
  uint32_t mv         = 0;         // the mean of the last minute, 0 before one
  int8_t   shown      = -1;        // the step shown, -1 for none yet
  bool     warned_low      = false;
  bool     warned_critical = false;
};

// What feed() changed, for the log, the header and the warnings.
enum : uint8_t { EV_SUPPLY = 1, EV_STEP = 2, EV_LOW = 4, EV_CRITICAL = 8 };

// One window. On the battery the step shown never rises (less load lifts
// the voltage, not the charge), while charging it never falls; a change of
// supply starts both afresh. Only windows that agree with the supply count
// towards the minute, the one with the jump of a plug does not.
uint8_t feed(State& s, const Window& w);

// An update is refused on the battery below UPDATE_MIN_PCT and with a
// battery below CRITICAL_MV, charging or not. Free without a battery and
// before the supply is known.
bool updateBlocked(const State& s);

// The colour of the display (Jorg, 03.11.2026, Spec V62): green from
// GOOD_PCT, yellow from LOW_PCT to under GOOD_PCT, red below LOW_PCT and
// below CRITICAL_MV; the bolt green while charging.
enum Tone : uint8_t { TONE_HIDDEN = 0, TONE_GOOD, TONE_LOW, TONE_CRITICAL, TONE_CHARGING };
Tone toneOf(const State& s);

// Where the display goes in the 480 px header line: between the version,
// which ends at left_end, and the first chip, which begins at right_start,
// SLOT_MARGIN from both. On the centre of the screen where it fits there,
// otherwise in the middle of the gap; symbol and number (w_full), the symbol
// alone (w_symbol), or nothing.
constexpr int16_t SLOT_MARGIN = 6;
constexpr int16_t SCREEN_W    = 480;
enum Form : uint8_t { FORM_NONE = 0, FORM_SYMBOL, FORM_FULL };
struct Slot {
  int16_t x;
  Form    form;
};
Slot headerSlot(int16_t left_end, int16_t right_start, int16_t w_full, int16_t w_symbol);

}  // namespace bat
