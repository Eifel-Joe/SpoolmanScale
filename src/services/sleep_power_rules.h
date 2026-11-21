#pragma once

#include <stdint.h>

// ============================================================
//  SAVING POWER ON THE 7B: THE RULES
//
//  Spec 5.3 AD (V81, V82). Measured with a diagnosis firmware on a 7B on
//  20./21.11.2026: with only the backlight off the deep sleep drew the
//  battery down by 46 mV/h, less than a day on a full cell; with the reader,
//  the weighing and the SD card off as well by 11.6 mV/h.
//
//  The rules live here, without Arduino: a host test holds them to the spec
//  (test_sleep_power_rules.py), and app/sleep_power.cpp, which switches the
//  parts, asks this unit.
// ============================================================

namespace sleeppwr {

// ---- What goes off before the deep sleep ---------------------------------
//
// After the backlight, in this order. The display's converter (EXIO_LCD_VDD)
// and its reset stay as they are (V82): switched off, the panel's logic still
// hangs on 3.3 V while the converter, a boost stage, lets the 5 V through
// coil and diode, so the panel lies half powered and shows a halo at hard
// edges for about half an hour after the wake (cross-check 21.11.2026). So
// there is no step for the panel here at all.
enum Step : uint8_t {
  STEP_READER_LINE = 1,   // the PN532's reset line held low (RSTPD_N, hard power-down)
  STEP_READER_COMMAND,    // the PN532's PowerDown command, woken by the next I2C word
  STEP_SCALE,             // the NAU7802 powered down (PU_CTRL PUD and PUA cleared)
  STEP_LOG,               // one log line: what went off, what failed
  STEP_RS485_HIGH,        // GPIO16 held high: low, the RS485 driver draws up to 3.3 mA
  STEP_SD_DESELECT,       // the SD card deselected; nothing writes after this
};

constexpr uint8_t MAX_STEPS = 6;
struct Cuts {
  uint8_t n;
  Step    step[MAX_STEPS];
};

// The list for one sleep. reset_line_ok: the probe at the start found the
// reader's reset wire working (services/nfc_reset.h). Without it the line
// may hang in the air, so the sleep command is the only way.
Cuts sleepCuts(bool reset_line_ok);

// ---- The wake rule -------------------------------------------------------
//
// The touch line woke the 7B. The diagnosis night saw no wake without a
// touch, but the GT911 may pull its INT for other reasons, and each false
// start costs a full boot with panel and WiFi. So the chip is asked first:
// bit 7 of its status register 0x814E says a report is ready.
constexpr uint8_t  GT_STATUS_READY = 0x80;
// A finger the chip did not report starts the scale on the second touch:
// a second wake within this many seconds always starts.
constexpr uint32_t SECOND_WAKE_S   = 30;

enum Wake : uint8_t { WAKE_START = 0, WAKE_SLEEP_ON };

// gt_status: the byte at 0x814E, or -1 when the chip did not answer (then a
// finger may be there: start). now_s and last_s: seconds of the RTC clock,
// which runs on through the deep sleep; last_s is the last wake that slept
// on, have_last whether there was one. A clock that went backwards cannot
// say how long ago that was: start.
Wake wakeRule(int16_t gt_status, uint32_t now_s, uint32_t last_s, bool have_last);

// ---- The reader on the battery -------------------------------------------
//
// On the battery the reader is on only while something lies on the scale:
// from LOAD_G either way (-15 g is what was taken off after the tare), off
// PARK_AFTER_MS after the weight left. On while the scale waits for a tag
// to write or to find a spool by (E1 a), the weight does not matter then.
// Where the rule does not apply (on the mains, no battery, no weighing, no
// reader, the supply not yet known) the reader is on.
constexpr float    LOAD_G        = 15.0f;
constexpr uint32_t PARK_AFTER_MS = 10000;

struct Gate {
  bool     on       = true;    // the reader is powered and set up
  bool     counting = false;   // the weight has left, the count runs
  uint32_t since    = 0;       // millis() when the count began
};

enum Act : uint8_t { ACT_NONE = 0, ACT_OFF, ACT_ON };

// One tick. applies: the rule holds now (battery, weighing and reader
// there). Returns what to do with the reader; g.on is already updated.
Act readerGate(Gate& g, bool applies, bool waiting_for_tag, float weight_g, uint32_t now_ms);

// ---- NAU7802 -------------------------------------------------------------
//
// PU_CTRL (register 0x00): PUD (bit 1) and PUA (bit 2) cleared powers the
// digital and the analog part down (NAU7802 data sheet, PU_CTRL); the other
// bits stay. The library's begin() powers it up again at the next start.
uint8_t nauPowerDown(uint8_t pu_ctrl);

}  // namespace sleeppwr
