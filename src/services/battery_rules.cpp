#include "services/battery_rules.h"

namespace bat {

namespace {

struct Point {
  uint16_t mv;
  uint8_t  pct;
};

// Under the load of the running scale (display on, WiFi), rising voltage.
// Provisional: the open-circuit table of Maxim's application note 4189
// (Table 1, "a typical Li+ cell") less 90 mV, the load's drop estimated from
// the measurement (charging 4.01 V, on the battery 3.82 V a moment later).
// The runtime measurement on Jorg's 7B replaces it.
constexpr Point LOAD[] = {
  {3216, 0}, {3597, 10}, {3651, 20}, {3685, 30}, {3703, 40}, {3731, 50},
  {3794, 60}, {3855, 70}, {3918, 80}, {3996, 90}, {4087, 100},
};

// While charging at 580 mA: the same table 100 mV higher, the charge
// current's rise across the cell, and full where the charger holds 4.2 V.
// Provisional until the charge recorded on 03.11.2026 is evaluated.
constexpr Point CHARGE[] = {
  {3406, 0}, {3787, 10}, {3841, 20}, {3875, 30}, {3893, 40}, {3921, 50},
  {3984, 60}, {4045, 70}, {4108, 80}, {4186, 90}, {4200, 100},
};

template <unsigned N>
uint8_t lookup(const Point (&t)[N], uint32_t mv) {
  if (mv <= t[0].mv) return t[0].pct;
  if (mv >= t[N - 1].mv) return t[N - 1].pct;
  for (unsigned i = 1; i < N; i++) {
    if (mv <= t[i].mv) {
      const uint32_t dv = (uint32_t)(t[i].mv - t[i - 1].mv);
      const uint32_t dp = (uint32_t)(t[i].pct - t[i - 1].pct);
      return (uint8_t)(t[i - 1].pct + ((mv - t[i - 1].mv) * dp + dv / 2) / dv);
    }
  }
  return t[N - 1].pct;
}

}  // namespace

uint32_t mvOf(uint32_t raw) {
  return (raw * 9900u + 511u) / 1023u;
}

void windowClear(Window& w) {
  w.n   = 0;
  w.sum = 0;
  w.lo  = 0xFFFF;
  w.hi  = 0;
}

void windowAdd(Window& w, uint16_t raw) {
  w.n++;
  w.sum += raw;
  if (raw < w.lo) w.lo = raw;
  if (raw > w.hi) w.hi = raw;
}

uint32_t windowMv(const Window& w) {
  if (w.n == 0) return 0;
  const uint64_t den = (uint64_t)w.n * 1023u;
  return (uint32_t)(((uint64_t)w.sum * 9900u + den / 2) / den);
}

Supply classify(const Window& w, Supply now) {
  if (w.n < WINDOW_MIN_OK) return UNKNOWN;
  const uint32_t mv   = windowMv(w);
  const uint16_t span = (uint16_t)(w.hi - w.lo);
  if (mv < NO_BATTERY_MV) return NONE;
  if (span >= SPAN_PULSING && mv >= PULSE_MIN_MV) return NONE;
  if (span <= SPAN_QUIET) return CHARGING;
  if (span >= SPAN_BUSY) return BATTERY;
  return now;
}

uint8_t pctOnBattery(uint32_t mv) { return lookup(LOAD, mv); }
uint8_t pctCharging(uint32_t mv) { return lookup(CHARGE, mv); }

uint8_t stepOf(uint8_t pct) {
  const unsigned step = ((unsigned)pct + 5u) / 10u * 10u;
  return (uint8_t)(step > 100u ? 100u : step);
}

Icon iconOf(uint8_t shown) {
  if (shown >= 90) return ICON_FULL;
  if (shown >= 60) return ICON_3;
  if (shown >= 40) return ICON_2;
  if (shown >= 20) return ICON_1;
  return ICON_EMPTY;
}

uint8_t feed(State& s, const Window& w) {
  const Supply c = classify(w, s.supply);
  if (c == UNKNOWN) return 0;

  uint8_t ev = 0;
  if (c == s.cand) {
    if (s.cand_count < 255) s.cand_count++;
  } else {
    s.cand       = c;
    s.cand_count = 1;
  }
  if (s.cand_count >= CONFIRM && s.cand != s.supply) {
    s.supply    = s.cand;
    ev         |= EV_SUPPLY;
    s.hist_n    = 0;
    s.hist_head = 0;
    s.mv        = 0;
    s.shown     = -1;
    // Charged (or taken out): the next discharge warns again.
    if (s.supply != BATTERY) {
      s.warned_low      = false;
      s.warned_critical = false;
    }
  }
  if (s.supply != BATTERY && s.supply != CHARGING) return ev;
  if (c != s.supply) return ev;   // the window with the jump

  s.hist[s.hist_head] = windowMv(w);
  s.hist_head = (uint8_t)((s.hist_head + 1) % AVG_WINDOWS);
  if (s.hist_n < AVG_WINDOWS) s.hist_n++;
  uint64_t sum = 0;
  for (uint8_t i = 0; i < s.hist_n; i++) sum += s.hist[i];
  s.mv = (uint32_t)(sum / s.hist_n);

  const bool    on_battery = (s.supply == BATTERY);
  const uint8_t step = stepOf(on_battery ? pctOnBattery(s.mv) : pctCharging(s.mv));
  int8_t next = (int8_t)step;
  if (s.shown >= 0) {
    if (on_battery && s.shown < next) next = s.shown;
    if (!on_battery && s.shown > next) next = s.shown;
  }
  if (next != s.shown) {
    s.shown = next;
    ev |= EV_STEP;
  }
  if (on_battery) {
    if (!s.warned_low && s.shown <= (int8_t)LOW_PCT) {
      s.warned_low = true;
      ev |= EV_LOW;
    }
    if (!s.warned_critical && s.mv <= CRITICAL_MV) {
      s.warned_critical = true;
      ev |= EV_CRITICAL;
    }
  }
  return ev;
}

bool updateBlocked(const State& s) {
  if (s.supply == BATTERY && s.shown >= 0 && s.shown < (int8_t)UPDATE_MIN_PCT) return true;
  const bool cell = (s.supply == BATTERY || s.supply == CHARGING);
  return cell && s.hist_n > 0 && s.mv <= CRITICAL_MV;
}

Tone toneOf(const State& s) {
  if (s.supply == CHARGING) return TONE_CHARGING;
  if (s.supply != BATTERY || s.shown < 0) return TONE_HIDDEN;
  if (s.mv <= CRITICAL_MV) return TONE_CRITICAL;
  if (s.shown <= (int8_t)LOW_PCT) return TONE_LOW;
  return TONE_PLAIN;
}

Slot headerSlot(int16_t left_end, int16_t right_start, int16_t w_full, int16_t w_symbol) {
  const int16_t lo   = (int16_t)(left_end + SLOT_MARGIN);
  const int16_t hi   = (int16_t)(right_start - SLOT_MARGIN);
  const int16_t room = (int16_t)(hi - lo);
  Form    form;
  int16_t w;
  if (room >= w_full) {
    form = FORM_FULL;
    w    = w_full;
  } else if (room >= w_symbol) {
    form = FORM_SYMBOL;
    w    = w_symbol;
  } else {
    return Slot{0, FORM_NONE};
  }
  int16_t x = (int16_t)(SCREEN_W / 2 - w / 2);
  if (x < lo || x + w > hi) x = (int16_t)(lo + (room - w) / 2);
  return Slot{x, form};
}

}  // namespace bat
