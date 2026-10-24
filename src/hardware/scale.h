#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <stdint.h>

bool scaleHardwareBegin(TwoWire* wire, void (*calibration_wait_cb)() = nullptr);

// Does the ADC still acknowledge its address? Ask before trusting a reading:
// Adafruit_BusIO answers a failed transfer with -1, so every register of an
// absent NAU7802 reads back as all ones. available() sees the conversion ready
// bit set forever and read() returns a sample of -1, and neither can tell the
// caller that the chip is gone.
bool scaleHardwarePresent();

bool scaleHardwareAvailable();
int32_t scaleHardwareReadRaw();

// The name of the converter, for the web page that shows the raw count. The
// NAU7802 is the only one this firmware drives, so that is the whole answer;
// once a second kind is supported this is where the one that was found comes
// back.
const char* scaleHardwareName();
