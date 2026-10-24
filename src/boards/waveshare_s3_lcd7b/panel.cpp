#include "hardware/panel.h"

#include "boards/board.h"
#include "hardware/display.h"
#include "hardware/sd_logger.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_lcd_panel_ops.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_rgb.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <string.h>

#include "boards/waveshare_s3_lcd7/gt911.h"
#include "io_ch32v003.h"
#include "panel_scale.h"

// ============================================================
//  1024x600 OF GLASS FOR A 480x320 PICTURE
//
//  As on the 7, the panel has no memory of its own: the ESP32 clocks every
//  pixel out of a frame buffer in PSRAM. panelWrite() enlarges each strip
//  LVGL hands over into that buffer until the picture fills the glass, 32/15
//  across and 15/8 down, nearest neighbour: every glass pixel shows the one
//  picture pixel it lies on. panel_scale.h says why that rule keeps strips
//  of any size from leaving gaps or reaching into each other.
//
//  Not the 7's blending of neighbours. Its ratios make blocks of 3x2 picture
//  pixels; these make blocks of 15x8, and every redrawn area would have to
//  be widened to whole blocks of that size. The picture is drawn 13.8 %
//  wider than designed, against 11 % on the 7: the whole glass, no black
//  bars.
// ============================================================

static constexpr int PANEL_W = 1024;
static constexpr int PANEL_H = 600;
// Glass pixels per picture pixel, as a fraction: 1024/480 and 600/320.
static constexpr int32_t SCALE_X_NUM = 32, SCALE_X_DEN = 15;
static constexpr int32_t SCALE_Y_NUM = 15, SCALE_Y_DEN = 8;
static_assert(DISPLAY_W_PX * SCALE_X_NUM == PANEL_W * SCALE_X_DEN &&
              DISPLAY_H_PX * SCALE_Y_NUM == PANEL_H * SCALE_Y_DEN,
              "the enlarged picture is not the size of the panel");
static_assert(BOARD_PANEL_ALIGN_X == 1 && BOARD_PANEL_ALIGN_Y == 1,
              "panelWrite() takes strips of any size and wants them unrounded");

// The panel's timing. The porches and the clock edge are Waveshare's for this
// board (rgb_lcd_port.c). The pixel clock is not: Waveshare's demos run
// 30 MHz, the LVGL ones with the PSRAM at 120 MHz, which ESP-IDF offers for
// octal PSRAM only among its experimental features. This firmware runs the
// PSRAM at 80 MHz, where Espressif puts the limit near 22 MHz (ESP FAQ,
// LCD), and the 7 measured some 30 MB a second out of it (see
// boards/waveshare_s3_lcd7/panel.cpp). 14 MHz at two bytes a pixel asks for
// 28 MB a second. With these long porches a line is 1386 clocks and a frame
// 661 lines, so 14 MHz gives 15.3 frames a second: slow for animation, enough
// for a scale. A slice of ten lines lasts 990 us on the glass and takes about
// 670 us to copy at the 7's rate (computed, not measured).
static constexpr uint32_t LCD_PCLK_HZ     = 14000000;
static constexpr uint32_t LCD_HSYNC_PULSE = 162;
static constexpr uint32_t LCD_HSYNC_BACK  = 152;
static constexpr uint32_t LCD_HSYNC_FRONT = 48;
static constexpr uint32_t LCD_VSYNC_PULSE = 45;
static constexpr uint32_t LCD_VSYNC_BACK  = 13;
static constexpr uint32_t LCD_VSYNC_FRONT = 3;

// The frame buffer is in PSRAM, and PSRAM shares its bus with the flash. Read
// from there directly, every flash write (a setting, a log line, an update)
// and every burst of WiFi stalls the pixel clock and the picture jumps
// sideways. With bounce buffers the driver copies the frame through two small
// buffers in internal RAM, ten lines at a time, and the clock never waits.
// A flash write still holds the copy up for a moment; the driver notices the
// missing turn and starts the next frame in step again, which needs
// CONFIG_LCD_RGB_RESTART_IN_VSYNC off. This board's environment extends the
// 7's in platformio.ini and has those options from there.
//
// The copy runs in an interrupt, and an interrupt belongs to the core that
// asked for it. That is core 1, the loop task's, because panelBegin() is
// called from setup(): on core 0 it would wait behind WiFi and Bluetooth,
// which keep interrupts off for longer than a slice lasts.
static constexpr size_t LCD_BOUNCE_LINES = 10;
static constexpr size_t LCD_DMA_BURST_BYTES = 64;

static esp_lcd_panel_handle_t s_panel = nullptr;
static uint16_t* s_fb = nullptr;
static bool s_touch_ok = false;
// One to one instead of enlarged, see panelSetNative().
static bool s_native = false;
static_assert(BOARD_NATIVE_W == PANEL_W && BOARD_NATIVE_H == PANEL_H,
              "BOARD_NATIVE_W and _H are not the panel's size");

// The picture column every glass column shows, and the picture line every
// glass line shows: scaleSource() once per glass pixel along each edge, in
// panelBegin(), so that panelWrite() divides nothing. 3.2 kB of internal RAM.
static_assert(DISPLAY_W_PX <= UINT16_MAX && DISPLAY_H_PX <= UINT16_MAX,
              "a picture coordinate does not fit the tables");
static uint16_t s_src_col[PANEL_W];
static uint16_t s_src_row[PANEL_H];

static void buildScaleTables() {
  for (int32_t o = 0; o < PANEL_W; o++) {
    s_src_col[o] = (uint16_t)scaleSource(o, SCALE_X_NUM, SCALE_X_DEN);
  }
  for (int32_t o = 0; o < PANEL_H; o++) {
    s_src_row[o] = (uint16_t)scaleSource(o, SCALE_Y_NUM, SCALE_Y_DEN);
  }
}

// A restart resets the processor and not the peripherals: the panel's DMA
// would go on reading the frame out of PSRAM while the bootloader loads the
// firmware over the same bus. On the 7 that boot stalled every time, until
// the hardware watchdog reset the chip a few seconds later (10.10.2026). So
// the panel is stopped first, dark, whoever asks for the restart. A restart
// does not reset the expander; boardEarlyInit() takes the panel's power and
// reset back down before panelBegin() runs again.
static void panelStop() {
  io_ch32v003::write(hw_pins::EXIO_BACKLIGHT, false);
  if (!s_panel) return;
  s_fb = nullptr;
  esp_lcd_panel_del(s_panel);
  s_panel = nullptr;
}

static bool rgbBegin() {
  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz = LCD_PCLK_HZ;
  cfg.timings.h_res = PANEL_W;
  cfg.timings.v_res = PANEL_H;
  cfg.timings.hsync_pulse_width = LCD_HSYNC_PULSE;
  cfg.timings.hsync_back_porch  = LCD_HSYNC_BACK;
  cfg.timings.hsync_front_porch = LCD_HSYNC_FRONT;
  cfg.timings.vsync_pulse_width = LCD_VSYNC_PULSE;
  cfg.timings.vsync_back_porch  = LCD_VSYNC_BACK;
  cfg.timings.vsync_front_porch = LCD_VSYNC_FRONT;
  cfg.timings.flags.pclk_active_neg = 1;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = 1;
  cfg.bounce_buffer_size_px = PANEL_W * LCD_BOUNCE_LINES;
  cfg.dma_burst_size = LCD_DMA_BURST_BYTES;
  cfg.hsync_gpio_num = hw_pins::LCD_HSYNC;
  cfg.vsync_gpio_num = hw_pins::LCD_VSYNC;
  cfg.de_gpio_num    = hw_pins::LCD_DE;
  cfg.pclk_gpio_num  = hw_pins::LCD_PCLK;
  cfg.disp_gpio_num  = -1;
  for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = hw_pins::LCD_DATA[i];
  cfg.flags.fb_in_psram = 1;

  esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &s_panel);
  if (err == ESP_OK) err = esp_lcd_panel_reset(s_panel);
  if (err == ESP_OK) err = esp_lcd_panel_init(s_panel);
  if (err != ESP_OK) {
    Serial.printf("Panel: the RGB driver refused, error 0x%x\n", (unsigned)err);
    return false;
  }
  void* fb = nullptr;
  if (esp_lcd_rgb_panel_get_frame_buffer(s_panel, 1, &fb) != ESP_OK || !fb) {
    Serial.println("Panel: no frame buffer");
    return false;
  }
  s_fb = (uint16_t*)fb;
  memset(s_fb, 0, (size_t)PANEL_W * PANEL_H * sizeof(uint16_t));
  esp_register_shutdown_handler(panelStop);
  return true;
}

// The GT911 takes its I2C address from the level of its INT line when its
// reset ends: low gives 0x5D. Here INT is low before the reset begins, the
// reset lasts 100 ms, and INT stays low for 200 ms after it while the chip
// starts, against 10 and 70 ms on the 7. Waveshare's driver for this board
// (touch_gt911_init() in gt911.c) is not the same: it holds the reset for
// 200 ms and pulls INT low only halfway through. What the two share is what
// decides the address, INT low when the reset ends, and the 200 ms after it.
// The line is the chip's output afterwards, and Waveshare's driver asks the
// chip for its configuration right then, as gt911::begin() does here.
static void touchBegin() {
  static constexpr uint32_t RESET_HOLD_MS = 100;
  static constexpr uint32_t BOOT_MS       = 200;
  pinMode(hw_pins::TOUCH_INT, OUTPUT);
  digitalWrite(hw_pins::TOUCH_INT, LOW);
  io_ch32v003::write(hw_pins::EXIO_TOUCH_RST, false);
  delay(RESET_HOLD_MS);
  io_ch32v003::write(hw_pins::EXIO_TOUCH_RST, true);
  delay(BOOT_MS);
  pinMode(hw_pins::TOUCH_INT, INPUT);
  s_touch_ok = gt911::begin(Wire);
  if (s_touch_ok) {
    Serial.printf("Touch: GT911 ready, reports %u x %u\n",
                  (unsigned)gt911::maxX(), (unsigned)gt911::maxY());
    // In the log as well: it is the one place the glass says how large it is,
    // and a panel of another size shows up here before anywhere else.
    logSDf("Touch: GT911 ready, reports %u x %u (panel driven at %d x %d)",
           (unsigned)gt911::maxX(), (unsigned)gt911::maxY(), PANEL_W, PANEL_H);
  } else {
    Serial.println("Touch: GT911 does not answer");
    logSD("Touch: GT911 does not answer");
  }
}

bool panelBegin() {
  static constexpr uint32_t LCD_POWER_SETTLE_MS = 20;
  static constexpr uint32_t LCD_RESET_SETTLE_MS = 20;
  buildScaleTables();
  // boardEarlyInit() has held the panel unpowered and in reset since
  // power-up. Its voltages first, then out of reset, then the clock.
  io_ch32v003::write(hw_pins::EXIO_LCD_VDD, true);
  delay(LCD_POWER_SETTLE_MS);
  io_ch32v003::write(hw_pins::EXIO_LCD_RST, true);
  delay(LCD_RESET_SETTLE_MS);
  const bool lcd_ok = rgbBegin();
  if (!lcd_ok) logSD("Panel: the RGB driver did not start, see the serial log");
  // What the panel costs in internal RAM is its two bounce buffers. The
  // figure after them is what the rest of the firmware starts with. On the
  // serial log as well: the glass may still be dark, and the SD card missing.
  char panel_line[96];
  snprintf(panel_line, sizeof(panel_line),
           "Panel: %d x %d at %u MHz, %u bounce lines, internal heap %u free",
           PANEL_W, PANEL_H, (unsigned)(LCD_PCLK_HZ / 1000000), (unsigned)LCD_BOUNCE_LINES,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  Serial.println(panel_line);
  logSD(panel_line);
  touchBegin();
  // Only now, over a frame buffer that is black: no flash of whatever the
  // PSRAM held.
  if (lcd_ok) io_ch32v003::write(hw_pins::EXIO_BACKLIGHT, true);
  return lcd_ok;
}

// Every glass pixel the strip covers takes the picture pixel it lies on.
// scaleSpan() says which glass pixels those are, the two tables which
// picture pixel each one shows, and all of them lie inside the strip: no
// read before or after it, whatever x, y, w and h LVGL chose. A glass line
// that shows the same picture line as the one above it is a copy of that
// line, 280 of the 600 in a full frame.
void panelWrite(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* px) {
  if (!s_fb) return;
  if (s_native) {
    for (int32_t row = 0; row < h; row++) {
      memcpy(s_fb + (y + row) * PANEL_W + x, px + row * w, (size_t)w * sizeof(uint16_t));
    }
    return;
  }
  const ScaleSpan cols = scaleSpan(x, x + w, SCALE_X_NUM, SCALE_X_DEN);
  const ScaleSpan rows = scaleSpan(y, y + h, SCALE_Y_NUM, SCALE_Y_DEN);
  const int32_t out_w = cols.end - cols.start;
  const uint16_t* src_col = s_src_col + cols.start;
  for (int32_t oy = rows.start; oy < rows.end; oy++) {
    uint16_t* dst = s_fb + oy * PANEL_W + cols.start;
    if (oy > rows.start && s_src_row[oy] == s_src_row[oy - 1]) {
      memcpy(dst, dst - PANEL_W, (size_t)out_w * sizeof(uint16_t));
      continue;
    }
    const uint16_t* src = px + (s_src_row[oy] - y) * w;
    for (int32_t i = 0; i < out_w; i++) dst[i] = src[src_col[i] - x];
  }
}

static int16_t clampTo(int32_t v, int32_t max) {
  if (v < 0) return 0;
  if (v > max) return (int16_t)max;
  return (int16_t)v;
}

// From the chip's coordinates straight to the UI's: the picture covers the
// whole glass, so the two only differ in scale.
uint8_t panelTouchRead(PanelTouchPoint* points, uint8_t max_points) {
  static constexpr uint8_t READ_MAX = 5;
  if (!s_touch_ok) return 0;
  gt911::Point raw[READ_MAX];
  if (max_points > READ_MAX) max_points = READ_MAX;
  const uint8_t n = gt911::read(raw, max_points);
  const int32_t chip_w = gt911::maxX() ? gt911::maxX() : PANEL_W;
  const int32_t chip_h = gt911::maxY() ? gt911::maxY() : PANEL_H;
  const int32_t out_w = s_native ? PANEL_W : DISPLAY_W_PX;
  const int32_t out_h = s_native ? PANEL_H : DISPLAY_H_PX;
  for (uint8_t i = 0; i < n; i++) {
    points[i].x = clampTo((int32_t)raw[i].x * out_w / chip_w, out_w - 1);
    points[i].y = clampTo((int32_t)raw[i].y * out_h / chip_h, out_h - 1);
  }
  return n;
}

void panelSetNative(bool on) { s_native = on; }

const uint16_t* panelGlass() { return s_fb; }

// Never called: BOARD_BACKLIGHT_PWM is 0 and the display dims the picture.
void panelSetBrightness(uint8_t brightness) {
  (void)brightness;
}

void panelBacklightOff() {
  io_ch32v003::write(hw_pins::EXIO_BACKLIGHT, false);
}

void panelBacklightOn(uint8_t brightness) {
  (void)brightness;
  io_ch32v003::write(hw_pins::EXIO_BACKLIGHT, true);
}

// The expander stays powered in deep sleep, and the CH422G on the 7 keeps its
// outputs there; whether this firmware does too, only the board can show. So
// the backlight is switched off here, or it could light a dead panel. The
// touch chip stays awake as well and moves its INT line on a touch: away from
// the level it rests at, which depends on how the chip was configured.
void panelPrepareDeepSleep() {
  // The backlight write and the touch-status clear share the touch chip's
  // bus, and either one can pull INT down for a moment. Do them first, then
  // wait until the line is back at its idle level. The digital pull-up dies
  // in deep sleep; without the RTC pull-up the open-drain line falls and the
  // wake, which is waiting for that fall, fires at once.
  io_ch32v003::write(hw_pins::EXIO_BACKLIGHT, false);
  if (s_touch_ok) gt911::clearStatus();
  const gpio_num_t pin = (gpio_num_t)hw_pins::TOUCH_INT;
  const bool idle_high = s_touch_ok && gt911::intIdleHigh();
  const int wake_level = idle_high ? 0 : 1;
  rtc_gpio_init(pin);
  rtc_gpio_set_direction(pin, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pulldown_dis(pin);
  rtc_gpio_pullup_en(pin);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  int level = 0;
  for (int i = 0; i < 20; i++) {
    delay(10);
    level = gpio_get_level(pin);
    if (level != wake_level) break;
  }
  logSDf("Deep sleep: INT %d, touch %s, idle %s, wake on %d",
         level, s_touch_ok ? "ok" : "not ready",
         idle_high ? "high" : "low", wake_level);
  if (!s_touch_ok || level == wake_level) {
    logSD("Deep sleep: touch wake not armed");
    return;
  }
  esp_sleep_enable_ext0_wakeup(pin, wake_level);
}
