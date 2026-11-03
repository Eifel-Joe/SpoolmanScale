#include "hardware/battery.h"

#include "io_ch32v003.h"

// VBAT through R18 20 k / R19 10 k on the expander's ADC (Spec 5.3 AB,
// sch-7b-battery-adc.png). The expander shares the board bus with the touch
// chip; both are only spoken to from the main loop, which is also where
// services/battery.cpp asks for this.
bool batteryReadRaw(uint16_t& raw) {
  return io_ch32v003::readAdc(raw);
}
