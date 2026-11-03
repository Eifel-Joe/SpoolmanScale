#include "services/battery.h"

#include "boards/board.h"

#if BOARD_BATTERY_SENSE
#include <Arduino.h>

#include "hardware/battery.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "ui/header_status.h"
#include "ui/info_popup.h"
#endif

static bat::State s_state;
static uint32_t   s_last_mv   = 0;
static uint16_t   s_last_span = 0;

void batteryLastWindow(uint32_t& mv, uint16_t& span) {
  mv   = s_last_mv;
  span = s_last_span;
}

const bat::State& batteryState() { return s_state; }

const char* batterySupplyName() {
  switch (s_state.supply) {
    case bat::NONE:     return "none";
    case bat::BATTERY:  return "battery";
    case bat::CHARGING: return "charging";
    default:            return "unknown";
  }
}

#if BOARD_BATTERY_SENSE

static bat::Window s_win       = {0, 0, 0xFFFF, 0};
static uint16_t    s_reads     = 0;   // readings in this window, answered or not
static uint32_t    s_last_read = 0;

#if defined(SSC_ACCEPTANCE)
static BatterySim s_sim    = BAT_SIM_OFF;
static uint32_t   s_sim_mv = 0;
static bool       s_sim_up = false;

// Around the voltage, alternately up and down by half the span the board
// showed: 26 counts on the battery (the boost's ripple), 8 while charging;
// nothing at all without a battery, as at the "USB" connector.
static uint16_t simRaw() {
  if (s_sim == BAT_SIM_NONE) return 0;
  const uint16_t mid  = (uint16_t)((s_sim_mv * 1023u + 4950u) / 9900u);
  const uint16_t half = (s_sim == BAT_SIM_BATTERY) ? 13 : 4;
  s_sim_up = !s_sim_up;
  return s_sim_up ? (uint16_t)(mid + half) : (uint16_t)(mid > half ? mid - half : 0);
}

void batterySimSet(BatterySim mode, uint32_t mv) {
  s_sim    = mode;
  s_sim_mv = mv;
}
#endif

static bool readOne(uint16_t& raw) {
#if defined(SSC_ACCEPTANCE)
  if (s_sim != BAT_SIM_OFF) {
    raw = simRaw();
    return true;
  }
#endif
  return batteryReadRaw(raw);
}

void batteryTick(uint32_t now_ms) {
  if (now_ms - s_last_read < bat::READ_MS) return;
  s_last_read = now_ms;
  uint16_t raw = 0;
  if (readOne(raw)) bat::windowAdd(s_win, raw);
  if (++s_reads < bat::WINDOW_READS) return;

  const bat::Tone tone_before = bat::toneOf(s_state);
  const uint8_t   ev          = bat::feed(s_state, s_win);
  s_last_mv   = bat::windowMv(s_win);
  s_last_span = s_win.n ? (uint16_t)(s_win.hi - s_win.lo) : 0;
  bat::windowClear(s_win);
  s_reads = 0;

  if (ev & bat::EV_SUPPLY) {
    logSDf("Battery: %s, %u mV", batterySupplyName(), (unsigned)s_state.mv);
  }
  if (ev & bat::EV_STEP) {
    logSDf("Battery: %d %%, %u mV, %s", s_state.shown, (unsigned)s_state.mv, batterySupplyName());
  }
  // The graver one only, when both come at once (a boot on an almost empty
  // battery): the second popup would replace the first anyway.
  if (ev & bat::EV_CRITICAL) {
    logSDf("Battery: almost empty, %u mV", (unsigned)s_state.mv);
    showInfoPopup(STR_BAT_CRIT_TITLE, STR_BAT_CRIT_TEXT, INFO_WARN);
  } else if (ev & bat::EV_LOW) {
    logSDf("Battery: low, %d %%", s_state.shown);
    showInfoPopup(STR_BAT_LOW_TITLE, STR_BAT_LOW_TEXT, INFO_WARN);
  }
  if (ev || bat::toneOf(s_state) != tone_before) updateHeaderBattery();
}

bool batteryUpdateBlocked() {
  return bat::updateBlocked(s_state);
}

#else  // no battery input on this board

void batteryTick(uint32_t) {}
bool batteryUpdateBlocked() { return false; }

#endif
