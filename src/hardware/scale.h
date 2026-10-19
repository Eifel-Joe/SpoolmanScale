#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <stdint.h>

// The load cell's converter: a NAU7802 on the I2C bus, or an HX711 on two
// pins of the I/O connector (hw_pins::HX711_SCK, HX711_DOUT). The NAU7802 is
// asked first; without it the HX711 is looked for, and once found it is
// remembered, so the next boot keeps its pins away from the NFC reader.
bool scaleHardwareBegin(TwoWire* wire, void (*calibration_wait_cb)() = nullptr);

// Does the ADC still answer? Ask before trusting a reading: Adafruit_BusIO
// answers a failed transfer with -1, so every register of an absent NAU7802
// reads back as all ones. available() sees the conversion ready bit set
// forever and read() returns a sample of -1, and neither can tell the caller
// that the chip is gone. An HX711 answers by finishing conversions.
bool scaleHardwarePresent();

bool scaleHardwareAvailable();
int32_t scaleHardwareReadRaw();

// An HX711 was found on this device, now or on an earlier boot. Its pins are
// then the scale's: the PN532 gets no reset line and the reset probe is off.
bool scaleHx711Fitted();
// "NAU7802", "HX711" or "-" for logs and the status page.
const char* scaleHardwareName();
