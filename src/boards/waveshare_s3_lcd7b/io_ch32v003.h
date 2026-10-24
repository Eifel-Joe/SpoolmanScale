#pragma once

#include <Wire.h>
#include <stdint.h>

// The 7B's I/O expander: a CH32V003 microcontroller with Waveshare's own
// firmware, the eight pins EXIO0..7 as outputs and a PWM for the backlight.
// The firmware is not published; all that is known of it is Waveshare's
// driver in the board's demo repository (io_extension.h and .c).
//
// It is not the 7's CH422G and does not speak like one. The CH422G has no
// address of its own and takes each register as an I2C address (0x24 mode,
// 0x38 outputs). The CH32V003 has one address, 0x24, and the register is the
// first byte written to it: 0x02 direction, 0x03 outputs, 0x05 PWM
// (io_extension.h, lines 32 to 39). The 7's driver never reached it, and the
// 7's firmware on this board said the expander does not answer.
//
// The outputs are always written as the whole byte, so the last byte written
// is kept here and a change of one pin sends all eight.
namespace io_ch32v003 {

// The levels, every pin an output, the levels again, so that no pin drives
// a level the chip held from before (EXIO5 high would take the USB
// connector away). False unless the direction and the second levels are
// acknowledged.
bool begin(TwoWire& wire, uint8_t outputs);

// One pin, by bit number. False when the write did not go through.
bool write(uint8_t bit, bool on);

// The backlight PWM, 0..255, inverted: 0 is full brightness. Waveshare's
// demo starts at 0 and turns its slider round (15_LVGL_SLIDER, lvgl_demo.c).
bool pwm(uint8_t value);

}
