#include "boards/board.h"

#include <Arduino.h>
#include <Wire.h>

#include "io_ch32v003.h"
#include "hardware/sd_logger.h"

// What the expander holds from the first moment: the panel unpowered and in
// reset, the touch chip in reset and the backlight off until panelBegin()
// brings them up in order, the SD card selected, and GPIO19/20 on the USB
// connector rather than on the CAN transceiver. All of that is every bit low.
static constexpr uint8_t EXIO_AT_BOOT = 0x00;
static constexpr uint32_t BOARD_I2C_HZ = 400000;
// Inverted on this board: 0 is the backlight at full strength. The display
// dims the picture instead, see BOARD_BACKLIGHT_PWM.
static constexpr uint8_t BACKLIGHT_PWM_FULL = 0;

// High would hand GPIO19/20 to the CAN transceiver, and with them the USB
// connector that the log and every flash go through.
static_assert((EXIO_AT_BOOT & (1u << hw_pins::EXIO_USB_SEL)) == 0,
              "the expander must start with GPIO19/20 on the USB connector");

void boardEarlyInit() {
  Wire.begin(hw_pins::BOARD_SDA, hw_pins::BOARD_SCL, BOARD_I2C_HZ);
  if (io_ch32v003::begin(Wire, EXIO_AT_BOOT) && io_ch32v003::pwm(BACKLIGHT_PWM_FULL)) {
    Serial.println("Board: CH32V003 expander ready");
    logSD("Board: CH32V003 expander ready");
  } else {
    // Without it there is no panel power, no backlight, no panel reset and no
    // SD card. The boot goes on: the web interface and the log still say
    // what happened.
    Serial.println("Board: CH32V003 expander does not answer on the board I2C bus");
    logSD("Board: CH32V003 expander does not answer on the board I2C bus");
  }
}
