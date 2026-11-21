#include "app/sleep_power.h"

#if BOARD_SLEEP_CUTS
#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_attr.h>
#include <esp_sleep.h>
#include <time.h>

#include "app/app_state.h"
#include "boards/waveshare_s3_lcd7b/io_ch32v003.h"
#include "hardware/nfc.h"
#include "hardware/scale.h"
#include "hardware/sd_logger.h"
#include "services/battery.h"
#include "services/nfc_reset.h"
#include "services/sleep_power_rules.h"
#include "services/user_options.h"

// What has to outlive the deep sleep. RTC memory survives the sleep but not a
// power-up, where it holds whatever it held: the magic tells the two apart.
struct SleepRtc {
  uint32_t magic;
  uint8_t  armed;         // the touch line was armed for the last sleep
  uint8_t  wake_level;    // and wakes at this level
  uint8_t  have_last;     // a wake slept on before, at last_s
  uint32_t last_s;
  uint16_t slept_on;      // wakes without a touch since the last start
};
static constexpr uint32_t RTC_MAGIC = 0x53504F57;   // "SPOW"
static RTC_DATA_ATTR SleepRtc s_rtc;

static const gpio_num_t READER_RST = (gpio_num_t)hw_pins::PN532_RESET_WIRE;
// GPIO16: the SD driver's stand-in chip select and the net RS485_RXD, the
// SP3485's DI. Held high through the sleep: low, the driver sinks up to
// 3.3 mA into it (meta repo docs/recherche/2026-11-20-akku-schlaf).
static const gpio_num_t RS485_TX = (gpio_num_t)hw_pins::SD_CS;

// ---- the start --------------------------------------------------------------

static void releaseHolds() {
  gpio_hold_dis((gpio_num_t)hw_pins::PN532_RESET_WIRE);
  gpio_hold_dis((gpio_num_t)hw_pins::SD_CS);
}

// Seconds of the system clock, which the RTC timer carries through the deep
// sleep (before the first NTP sync it counts from the power-up).
static uint32_t nowSeconds() {
  const time_t t = time(nullptr);
  return t > 0 ? (uint32_t)t : 0;
}

// The GT911's status register 0x814E, bit 7 a report ready. The chip answers
// at 0x5D or 0x14, whichever its INT level chose at its reset. -1 when
// neither answers.
static int16_t touchStatus() {
  static const uint8_t addrs[] = { 0x5D, 0x14 };
  for (uint8_t a : addrs) {
    Wire.beginTransmission(a);
    Wire.write(0x81);
    Wire.write(0x4E);
    if (Wire.endTransmission(false) != 0) continue;
    if (Wire.requestFrom(a, (uint8_t)1) != 1) continue;
    return (int16_t)Wire.read();
  }
  return -1;
}

// The touch line armed again as panelPrepareDeepSleep() arms it. False when
// the line will not leave the waking level: asleep like that the scale would
// never wake again, so it starts instead.
static bool armTouchAgain() {
  const gpio_num_t pin = (gpio_num_t)hw_pins::TOUCH_INT;
  rtc_gpio_init(pin);
  rtc_gpio_set_direction(pin, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pulldown_dis(pin);
  rtc_gpio_pullup_en(pin);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  for (int i = 0; i < 20; i++) {
    if (rtc_gpio_get_level(pin) != s_rtc.wake_level) {
      esp_sleep_enable_ext0_wakeup(pin, s_rtc.wake_level);
      return true;
    }
    delay(10);
  }
  return false;
}

static bool wokenByTouchLine() {
  return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0 && s_rtc.magic == RTC_MAGIC && s_rtc.armed;
}

// A wake by the touch line: asks the chip before anything resets it. The
// held lines, the powered-down reader and weighing and the deselected card
// are all as the last sleep left them, so going back is only the line again.
// Nothing is written here; sleepPowerReport() tells the count at the next
// real start.
static void sleepOnWithoutTouch() {
  Wire.begin(hw_pins::BOARD_SDA, hw_pins::BOARD_SCL, 400000);
  const uint32_t now = nowSeconds();
  const sleeppwr::Wake w = sleeppwr::wakeRule(touchStatus(), now, s_rtc.last_s, s_rtc.have_last != 0);
  if (w == sleeppwr::WAKE_START) return;
  s_rtc.last_s    = now;
  s_rtc.have_last = 1;
  if (s_rtc.slept_on < 0xFFFF) s_rtc.slept_on++;
  if (!armTouchAgain()) return;
  esp_deep_sleep_start();
}

void sleepPowerEarly() {
  if (wokenByTouchLine()) sleepOnWithoutTouch();
  s_rtc.armed = 0;
  releaseHolds();
}

void sleepPowerReport() {
  if (s_rtc.magic != RTC_MAGIC) return;
  if (s_rtc.slept_on) {
    logSDf("Deep sleep: woken %u times without a touch, slept on", (unsigned)s_rtc.slept_on);
    s_rtc.slept_on = 0;
  }
}

void sleepPowerArmed(uint8_t wake_level) {
  s_rtc.magic      = RTC_MAGIC;
  s_rtc.armed      = 1;
  s_rtc.wake_level = wake_level;
}

// ---- the reader while the scale runs ----------------------------------------

static sleeppwr::Gate s_gate;
static bool     s_parked    = false;
static bool     s_by_line   = false;   // parked by the reset line, not the command
static bool     s_wanted    = false;
static uint32_t s_wanted_at = 0;
// How long a scan request or the tag page in the browser keeps the reader on:
// as long as a scan request waits for its tag (SCAN_WAIT_MS in
// services/tag_write.cpp); the page asks every three seconds while it is open.
static constexpr uint32_t WANTED_MS = 30000;

static void readerPark() {
  s_by_line = nfcResetVerified();
  if (s_by_line) {
    // Open drain, as the reset probe drives it (services/nfc_reset.cpp).
    pinMode(hw_pins::PN532_RESET_WIRE, OUTPUT_OPEN_DRAIN);
    digitalWrite(hw_pins::PN532_RESET_WIRE, LOW);
    s_parked = true;
    logSD("NFC: reader off by its reset line (battery, nothing on the scale)");
    return;
  }
  if (nfcHardwarePowerDown()) {
    s_parked = true;
    logSD("NFC: reader off by its sleep command (battery, nothing on the scale)");
  } else {
    // It did not take the command: left on, the rule sees it as on.
    s_gate.on = true;
    logSD("NFC: reader did not take the sleep command, stays on");
  }
}

static void readerUnpark(const char* why) {
  if (!s_parked) return;
  if (s_by_line) digitalWrite(hw_pins::PN532_RESET_WIRE, HIGH);
  s_parked = false;
  // The first frame wakes a reader asleep by the command, so a second try
  // may be needed before it answers.
  uint32_t ver = 0;
  bool ok = nfcHardwareReinit(&ver);
  if (!ok) ok = nfcHardwareReinit(&ver);
  nfc_ok = ok;
  logSDf("NFC: reader on (%s)%s", why, ok ? "" : ", but it does not answer");
}

void sleepPowerReaderTick(float weight_g, bool waiting_for_tag) {
  const uint32_t now = millis();
  if (s_wanted && (uint32_t)(now - s_wanted_at) >= WANTED_MS) s_wanted = false;
  const bool applies = batteryState().supply == bat::BATTERY && g_scale_fitted && scl_ok && (nfc_ok || s_parked);
  const sleeppwr::Act act = sleeppwr::readerGate(s_gate, applies, waiting_for_tag || s_wanted, weight_g, now);
  if (act == sleeppwr::ACT_OFF) readerPark();
  else if (act == sleeppwr::ACT_ON) readerUnpark(!applies ? "rule off" : (waiting_for_tag || s_wanted) ? "waiting for a tag" : "weight");
}

bool sleepPowerReaderParked() { return s_parked; }

void sleepPowerReaderWake() {
  s_gate = sleeppwr::Gate();
  readerUnpark("probe");
}

void sleepPowerReaderWanted() {
  s_wanted    = true;
  s_wanted_at = millis();
}

// ---- the deep sleep -----------------------------------------------------------

// What each step did, for the one log line: a letter each, x when it failed,
// - when the part is not there. R reader by its line, N reader by its
// command, S weighing. RS485 and card come after the line.
void sleepPowerBeforeSleep() {
  const sleeppwr::Cuts cuts = sleeppwr::sleepCuts(nfcResetVerified());
  char what[16] = "";
  size_t n = 0;
  auto note = [&](char c) { if (n + 1 < sizeof(what)) { what[n++] = c; what[n] = 0; } };
  for (uint8_t i = 0; i < cuts.n; i++) {
    switch (cuts.step[i]) {
      case sleeppwr::STEP_READER_LINE:
        pinMode(hw_pins::PN532_RESET_WIRE, OUTPUT_OPEN_DRAIN);
        digitalWrite(hw_pins::PN532_RESET_WIRE, LOW);
        gpio_hold_en(READER_RST);
        note('R');
        break;
      case sleeppwr::STEP_READER_COMMAND:
        // Already asleep by the same command when the rule parked it.
        note(s_parked ? 'N' : !nfc_ok ? '-' : nfcHardwarePowerDown() ? 'N' : 'x');
        break;
      case sleeppwr::STEP_SCALE:
        note(!g_scale_fitted || !scl_ok ? '-' : scaleHardwarePowerDown() ? 'S' : 'x');
        break;
      case sleeppwr::STEP_LOG:
        logSDf("Deep sleep: off %s (R reader line, N reader command, S weighing, x failed, - not there; "
               "display stays on)", what);
        break;
      case sleeppwr::STEP_RS485_HIGH:
        pinMode(hw_pins::SD_CS, OUTPUT);
        digitalWrite(hw_pins::SD_CS, HIGH);
        gpio_hold_en(RS485_TX);
        break;
      case sleeppwr::STEP_SD_DESELECT:
        io_ch32v003::write(hw_pins::EXIO_SD_CS, true);
        break;
    }
  }
}

#else

void sleepPowerEarly() {}
void sleepPowerReport() {}
void sleepPowerArmed(uint8_t) {}
void sleepPowerBeforeSleep() {}
void sleepPowerReaderTick(float, bool) {}
bool sleepPowerReaderParked() { return false; }
void sleepPowerReaderWake() {}
void sleepPowerReaderWanted() {}

#endif
