"""Compile the HX711 pin rules once per board and check what each board decides."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include "hardware/scale_pins.h"

#if defined(BOARD_WT32_SC01_PLUS)
// The HX711 has pins of its own, next to the sensor bus, so NFC runs beside
// it - but they are the two pins the PN532's reset line used.
static_assert(!hx711SharesSensorBus(), "WT32: the HX711 is off the sensor bus");
static_assert(hx711TakesPn532Reset(), "WT32: the HX711 takes the reset pins");
static_assert(hw_pins::HX711_SCK == 12 && hw_pins::HX711_DOUT == 14, "WT32: GPIO12/14");
#elif defined(BOARD_WAVESHARE_S3_LCD7)
// No pin left but the sensor bus: the HX711 replaces it, the clock on TXD.
// The reader's reset on GPIO6 stays the reader's.
static_assert(hx711SharesSensorBus(), "Waveshare: the HX711 sits on the sensor bus");
static_assert(!hx711TakesPn532Reset(), "Waveshare: the reset line stays");
static_assert(hw_pins::HX711_SCK == hw_pins::I2C_EXT_SDA, "Waveshare: SCK on GPIO43");
static_assert(hw_pins::HX711_DOUT == hw_pins::I2C_EXT_SCL, "Waveshare: DT on GPIO44");
#else
#error "no board"
#endif

// The diagnosis names the pins of the board it is shown on.
static_assert(sizeof(BOARD_HX711_PINS_TEXT) > 1, "pin text for the diagnosis");

int main() { return 0; }
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    cpp.write_text(source)
    for board in ("BOARD_WT32_SC01_PLUS", "BOARD_WAVESHARE_S3_LCD7"):
        binary = Path(tmp) / board
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-D" + board,
                        "-I", str(root / "src"), str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print("hx711 pins: ok")
