#include "services/battery.h"

#include "boards/board.h"

#if BOARD_BATTERY_SENSE
#include <Arduino.h>
#include <esp_attr.h>

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
static int8_t      s_logged    = -1;  // the level of the last log line

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
    s_logged = -1;   // the new supply's first level gets its line
  }
  if ((ev & bat::EV_STEP) && bat::logWorthy(s_logged, s_state.shown)) {
    logSDf("Battery: %d %%, %u mV, %s", s_state.shown, (unsigned)s_state.mv, batterySupplyName());
    s_logged = s_state.shown;
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

// ---- the start on an empty battery (Spec V66) -------------------------
//
// Wurzel: the CS8501 stops its boost at 2.7 V and starts it again at 3.0 V.
//   After the runtime measurement on 03.11.2026 the 7B began to start 39
//   times in the 15 minutes before the supply came back: the cell recovered
//   over 3.0 V, the start pulled it under 2.7 V, each start wrote its lines
//   to the log in flash, none got as far as reading the battery (meta repo
//   docs/recherche/2026-11-03-beta20/protokolle/laufzeit-ende.txt).
// Fix: read it right after the expander is up, with the panel unpowered
//   and before the logger, the settings and the WiFi; an empty cell waits
//   here, dark, reading every second at the lowest clock, until a supply at
//   "UART" lifts it (Jorg: not a minute dark after plugging in).
// NOT-TO-DO: a deep sleep with a timer - the scale would stay dark for up to
//   that long after the supply came (Jorg, 03.11.2026). "Akku leer" on the
//   screen - panel and backlight pull the empty cell under 2.7 V at once,
//   the very start this avoids. Going on above START_EMPTY_MV again - a
//   resting cell creeps up and the starts would come back.
// Test: test/test_battery_rules.py (the rules), test_battery_wiring.py (8).
static constexpr uint8_t  START_READS     = 8;
static constexpr uint32_t START_READ_GAP  = 20;     // ms between the readings of one look
static constexpr uint32_t START_LOOK_MS   = 1000;   // one look a second while waiting
static constexpr uint8_t  START_GO_LOOKS  = 2;      // looks in a row that say "go on"
static uint32_t s_wait_ms     = 0;
static uint32_t s_wait_mv     = 0;
static uint32_t s_go_mv       = 0;
static bool     s_wait_sim    = false;

#if defined(SSC_ACCEPTANCE)
// The next start's reading, kept across the restart that shows it (RTC memory
// survives a software restart; after a power-on the magic is noise).
struct StartSim {
  uint32_t magic;
  uint16_t mv;
  uint16_t secs;
};
static constexpr uint32_t START_SIM_MAGIC = 0x53494D42UL;
static RTC_NOINIT_ATTR StartSim s_start_sim;

void batterySimForStart(uint32_t mv, uint32_t secs) {
  s_start_sim.mv    = (uint16_t)mv;
  s_start_sim.secs  = (uint16_t)secs;
  s_start_sim.magic = START_SIM_MAGIC;
}
#endif

// The mean of START_READS readings in millivolts, 0 when the chip did not answer.
static uint32_t startLook() {
  bat::Window w;
  bat::windowClear(w);
  for (uint8_t i = 0; i < START_READS; i++) {
    uint16_t raw = 0;
    if (batteryReadRaw(raw)) bat::windowAdd(w, raw);
    delay(START_READ_GAP);
  }
  return bat::windowMv(w);
}

void batteryStartCheck() {
  uint32_t sim_mv = 0;
  uint32_t sim_ms = 0;
#if defined(SSC_ACCEPTANCE)
  if (s_start_sim.magic == START_SIM_MAGIC) {
    s_start_sim.magic = 0;   // once
    sim_mv = s_start_sim.mv;
    sim_ms = (uint32_t)s_start_sim.secs * 1000u;
  }
#endif
  const uint32_t mv = sim_mv ? sim_mv : startLook();
  if (!bat::emptyAtStart(mv)) return;
  Serial.printf("Battery: empty at the start, %u mV%s, waiting dark for a supply\n",
                (unsigned)mv, sim_mv ? " (simulated)" : "");
  s_wait_mv  = mv;
  s_wait_sim = sim_mv != 0;
  const uint32_t cpu_mhz = getCpuFrequencyMhz();
  setCpuFrequencyMhz(80);
  const uint32_t t0 = millis();
  uint8_t go = 0;
  for (;;) {
    delay(START_LOOK_MS);
    const uint32_t now = (sim_mv && millis() - t0 < sim_ms) ? sim_mv : startLook();
    if (now == 0) continue;   // no answer this time: look again
    go = bat::goAfterWait(mv, now) ? (uint8_t)(go + 1) : 0;
    if (go >= START_GO_LOOKS) {
      s_go_mv = now;
      break;
    }
  }
  s_wait_ms = millis() - t0;
  setCpuFrequencyMhz(cpu_mhz);
}

void batteryStartReport() {
  if (!s_wait_mv) return;
  logSDf("Battery: the start waited %lu s dark on an empty battery, %u mV%s, went on at %u mV",
         (unsigned long)(s_wait_ms / 1000u), (unsigned)s_wait_mv, s_wait_sim ? " (simulated)" : "",
         (unsigned)s_go_mv);
}

#else  // no battery input on this board

void batteryStartCheck() {}
void batteryStartReport() {}
void batteryTick(uint32_t) {}
bool batteryUpdateBlocked() { return false; }

#endif
