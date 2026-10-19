#include "scale.h"

#include <Adafruit_NAU7802.h>

#include "app_config.h"
#include "hardware/pins.h"
#include "services/prefs_store.h"

enum ScaleAdc : uint8_t { ADC_NONE, ADC_NAU7802, ADC_HX711 };

static Adafruit_NAU7802 nau;
// Kept so the address can be probed without the caller handing the bus in
// again - see scaleHardwarePresent().
static TwoWire* nau_wire = nullptr;
static ScaleAdc s_adc = ADC_NONE;

// Give up on the internal calibration after roughly three seconds. This used
// to retry forever, which hung the whole boot on a defective load cell or a
// loose cable: no display, no WiFi, no web interface, nothing but a device that
// looks dead. Returning false instead lets app_boot.cpp carry on without the
// scale, so display, NFC and the filament manager still work and the red SCL!
// in the header says what is wrong. Do not turn this back into a bare while.
#define CAL_MAX_ATTEMPTS  30
#define CAL_RETRY_DELAY_MS 100

// ---- HX711 ---------------------------------------------------------------
// Data sheet: DOUT falls when a conversion is ready (10 or 80 per second, by
// the board's RATE pin). 24 clock pulses shift it out MSB first, a 25th
// selects channel A at gain 128 for the next one. A clock held high for more
// than 60 us powers the chip down, so the pulses must not be interrupted.
#define HX_KEY             "hx711"
// Up to 400 ms settling after power-up, then one conversion at 10 per second.
#define HX_FIND_MS         700
// Without a finished conversion for this long the HX711 counts as gone.
#define HX_ALIVE_MS        1000

static portMUX_TYPE s_hx_mux = portMUX_INITIALIZER_UNLOCKED;
static int8_t s_hx_fitted = -1;     // -1 not read from the preferences yet
static uint32_t s_hx_ready_ms = 0;
static int32_t s_hx_last = 0;

bool scaleHx711Fitted() {
  if (s_hx_fitted < 0) s_hx_fitted = prefsGetBool(HX_KEY, false) ? 1 : 0;
  return s_hx_fitted == 1;
}

static void hxRemember(bool fitted) {
  if (scaleHx711Fitted() == fitted) return;
  s_hx_fitted = fitted ? 1 : 0;
  prefsPutBool(HX_KEY, fitted);
}

static void hxPins() {
  pinMode(hw_pins::HX711_SCK, OUTPUT);
  // Low wakes the chip, and keeps it awake.
  digitalWrite(hw_pins::HX711_SCK, LOW);
  // Nothing on the line reads high: an absent HX711 never looks ready.
  pinMode(hw_pins::HX711_DOUT, INPUT_PULLUP);
}

static bool hxReady() {
  if (digitalRead(hw_pins::HX711_DOUT) != LOW) return false;
  s_hx_ready_ms = millis();
  return true;
}

static int32_t hxShiftIn() {
  uint32_t v = 0;
  portENTER_CRITICAL(&s_hx_mux);
  for (int i = 0; i < 25; i++) {
    digitalWrite(hw_pins::HX711_SCK, HIGH);
    delayMicroseconds(1);
    if (i < 24) v = (v << 1) | (digitalRead(hw_pins::HX711_DOUT) ? 1 : 0);
    digitalWrite(hw_pins::HX711_SCK, LOW);
    delayMicroseconds(1);
  }
  portEXIT_CRITICAL(&s_hx_mux);
  if (v & 0x800000) v |= 0xFF000000;   // 24 bit two's complement
  return (int32_t)v;
}

// Two conversions that look like an HX711's. A line that is merely held low
// (an old build's reset wire on EXT_IO5 reads low) shifts out zeros and stays
// low after the read; a real HX711 raises DOUT as soon as the 25th pulse ends.
static bool hxFind(void (*wait_cb)()) {
  hxPins();
  int good = 0;
  const uint32_t t0 = millis();
  while (good < 2 && millis() - t0 < HX_FIND_MS) {
    if (!hxReady()) {
      if (wait_cb) wait_cb(); else delay(10);
      continue;
    }
    const int32_t raw = hxShiftIn();
    delayMicroseconds(5);
    if (digitalRead(hw_pins::HX711_DOUT) != HIGH) return false;
    if (raw == 0 || raw == -1) return false;
    s_hx_last = raw;
    good++;
  }
  return good == 2;
}

// ---- both ----------------------------------------------------------------

static bool nauPresent() {
  if (!nau_wire) return false;
  nau_wire->beginTransmission(I2C_ADDR_NAU7802);
  return nau_wire->endTransmission() == 0;
}

static bool nauBegin(void (*calibration_wait_cb)()) {
  if (!nau.begin(nau_wire)) {
    Serial.println("NAU7802 answered but would not start");
    return false;
  }
  nau.setLDO(NAU7802_3V0);
  nau.setGain(NAU7802_GAIN_128);
  nau.setRate(NAU7802_RATE_10SPS);

  delay(300);
  for (int attempt = 0; attempt < CAL_MAX_ATTEMPTS; attempt++) {
    if (nau.calibrate(NAU7802_CALMOD_INTERNAL)) return true;
    if (calibration_wait_cb) {
      calibration_wait_cb();
    } else {
      Serial.println("Calibrate retry...");
      delay(CAL_RETRY_DELAY_MS);
    }
  }
  Serial.println("NAU7802 calibration timed out");
  return false;
}

bool scaleHardwareBegin(TwoWire* wire, void (*calibration_wait_cb)()) {
  nau_wire = wire;
  s_adc = ADC_NONE;

  // Asked separately so the two failure modes can be told apart. begin() also
  // returns false for a chip that answers and then refuses to start, and the
  // caller prints a different line for each.
  if (nauPresent()) {
    if (!nauBegin(calibration_wait_cb)) return false;
    s_adc = ADC_NAU7802;
    hxRemember(false);
    return true;
  }
  if (hxFind(calibration_wait_cb)) {
    s_adc = ADC_HX711;
    s_hx_ready_ms = millis();
    hxRemember(true);
    return true;
  }
  Serial.printf("Neither a NAU7802 on 0x%02X nor an HX711 on GPIO%d/%d\n",
                I2C_ADDR_NAU7802, hw_pins::HX711_SCK, hw_pins::HX711_DOUT);
  return false;
}

bool scaleHardwarePresent() {
  if (s_adc == ADC_HX711 || (s_adc == ADC_NONE && scaleHx711Fitted())) {
    if (s_adc == ADC_NONE) hxPins();
    return hxReady() || millis() - s_hx_ready_ms < HX_ALIVE_MS;
  }
  return nauPresent();
}

// True once the ADC has finished a new conversion. Reading without checking
// returns the previous sample again, which then enters the moving average a
// second time and weights it twice.
//
// The presence probe comes first. Without it this answers true for a chip that
// is not on the bus at all, because the failed register read is all ones and
// the conversion ready bit is one of them.
bool scaleHardwareAvailable() {
  if (s_adc == ADC_HX711) {
    // Something else may have raised the clock (the PN532 library pulses its
    // reset pin, which was this one before the HX711 was known).
    digitalWrite(hw_pins::HX711_SCK, LOW);
    return hxReady();
  }
  return nauPresent() && nau.available();
}

// The NAU7802 hands back its last conversion whenever asked. The HX711 does
// not: clocked while it converts, it shifts out half a sample and starts the
// next one late. So a new sample is taken only when one is ready, and the
// last one is answered otherwise, as the NAU7802 would.
int32_t scaleHardwareReadRaw() {
  if (s_adc == ADC_HX711) {
    if (hxReady()) s_hx_last = hxShiftIn();
    return s_hx_last;
  }
  return nau.read();
}

const char* scaleHardwareName() {
  switch (s_adc) {
    case ADC_NAU7802: return "NAU7802";
    case ADC_HX711:   return "HX711";
    default:          return "-";
  }
}
