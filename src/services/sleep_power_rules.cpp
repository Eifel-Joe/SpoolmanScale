#include "services/sleep_power_rules.h"

namespace sleeppwr {

Cuts sleepCuts(bool reset_line_ok) {
  Cuts c{};
  c.step[c.n++] = reset_line_ok ? STEP_READER_LINE : STEP_READER_COMMAND;
  c.step[c.n++] = STEP_SCALE;
  c.step[c.n++] = STEP_LOG;
  c.step[c.n++] = STEP_RS485_HIGH;
  c.step[c.n++] = STEP_SD_DESELECT;
  return c;
}

Wake wakeRule(int16_t gt_status, uint32_t now_s, uint32_t last_s, bool have_last) {
  if (gt_status < 0) return WAKE_START;
  if ((uint8_t)gt_status & GT_STATUS_READY) return WAKE_START;
  if (!have_last) return WAKE_SLEEP_ON;
  if (now_s < last_s) return WAKE_START;
  return (now_s - last_s <= SECOND_WAKE_S) ? WAKE_START : WAKE_SLEEP_ON;
}

Act readerGate(Gate& g, bool applies, bool waiting_for_tag, float weight_g, uint32_t now_ms) {
  const bool loaded = weight_g >= LOAD_G || weight_g <= -LOAD_G;
  if (!applies || waiting_for_tag || loaded) {
    g.counting = false;
    if (g.on) return ACT_NONE;
    g.on = true;
    return ACT_ON;
  }
  if (!g.on) return ACT_NONE;
  if (!g.counting) {
    g.counting = true;
    g.since    = now_ms;
    return ACT_NONE;
  }
  // Unsigned difference: holds across the wrap of millis() after 49 days.
  if ((uint32_t)(now_ms - g.since) < PARK_AFTER_MS) return ACT_NONE;
  g.on       = false;
  g.counting = false;
  return ACT_OFF;
}

uint8_t nauPowerDown(uint8_t pu_ctrl) {
  return (uint8_t)(pu_ctrl & ~((1u << 1) | (1u << 2)));
}

}  // namespace sleeppwr
