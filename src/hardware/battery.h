#pragma once

#include <stdint.h>

#include "boards/board.h"

// The board's battery input, where it has one (BOARD_BATTERY_SENSE). One
// ADC reading, 10 bit, of VBAT/3; false when the chip did not answer. Defined
// in the folder of a board that measures, nowhere else.
#if BOARD_BATTERY_SENSE
bool batteryReadRaw(uint16_t& raw);
#endif
