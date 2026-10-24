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
#include <esp_timer.h>
#include <stdint.h>
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
// board (rgb_lcd_port.c); with them a line is 1386 clocks and a frame 661
// lines. At 14 MHz, about 15 frames a second, the panel flickered, mostly
// along its edges and in its corners (24.10.2026; a video of it beat against
// the camera at 8.7 Hz). At 24 MHz, what Waveshare's own LVGL 9 demo runs, a
// slice of ten lines lasted 578 us on the glass, and the board measured
// 236..456 us for copying one, 20480 B (boot log 24.10.2026); the picture
// was better. At 28 MHz a slice lasts 495 us and the frame rate is 30.6 Hz:
// the slowest copy measured still fits, with less room. measureSliceCopy()
// goes on logging the copy at every boot, the figure to judge this clock by.
//
// Not 30 MHz, although ESPHome users run this board at that with the PSRAM
// at 80 MHz octal and the code running from it, as here (display.md, 1.1, in
// docs/recherche/2026-10-24-7b/ of the meta repo): a slice would last 462 us,
// 6 us more than the slowest copy measured. Against any clock above 22 MHz
// stand Espressif's FAQ, which puts the limit near there for octal PSRAM at
// 80 MHz, and a 7B ESPHome user without XIP who saw jitter under load at 25
// and 30 MHz (display.md 1.3). The glass decides.
static constexpr uint32_t LCD_PCLK_HZ     = 28000000;
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

// What the timing adds up to, porches included: the frame rate in tenths of a
// hertz and the time a slice lasts on the glass in microseconds, both rounded.
// 64-bit maths, because ten lines of clocks times a million is more than 32
// bits hold.
static constexpr uint64_t LCD_LINE_CLOCKS = PANEL_W + LCD_HSYNC_PULSE + LCD_HSYNC_BACK + LCD_HSYNC_FRONT;
static constexpr uint64_t LCD_FRAME_LINES = PANEL_H + LCD_VSYNC_PULSE + LCD_VSYNC_BACK + LCD_VSYNC_FRONT;
static constexpr uint64_t LCD_FRAME_CLOCKS = LCD_LINE_CLOCKS * LCD_FRAME_LINES;
static constexpr uint32_t LCD_FRAME_TENTHS_HZ =
    (uint32_t)((LCD_PCLK_HZ * 10ULL + LCD_FRAME_CLOCKS / 2) / LCD_FRAME_CLOCKS);
static constexpr uint32_t LCD_SLICE_US =
    (uint32_t)((LCD_BOUNCE_LINES * LCD_LINE_CLOCKS * 1000000ULL + LCD_PCLK_HZ / 2) / LCD_PCLK_HZ);

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
// reset back down before panelBegin() runs again. What the backlight write
// returns is not logged: this runs as the device restarts. write() tries it
// again by itself.
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

// Copies one slice out of the frame buffer eight times, with the panel already
// scanning it out, and logs how long that took next to the time a slice lasts
// on the glass (serial and SD log). It is an upper bound on what the driver's
// interrupt pays for the same copy, not that copy: a plain CPU memcpy, cold,
// with no other traffic on the bus. The driver preloads the next slice into the
// data cache at the end of every turn (Cache_Start_DCache_Preload in
// lcd_rgb_panel_fill_bounce_buffer(), IDF 5.5.5), so its interrupt mostly
// copies from cache.
// What pulls the other way, XIP code fetches and LVGL and WiFi in PSRAM, is
// not in this figure either.
//
// Judge by the slowest of the eight: the fastest may land on a slice the
// interrupt just preloaded, and the first run may include the write-back of
// lines the frame buffer's memset left dirty in the cache. The slowest below
// the time a slice lasts means the copy fits. Above it does not prove that
// the pixel clock is too fast; the glass decides.
//
// Every run reads another slice of the frame: the data cache would hand back
// one just read. Every run holds this core's interrupts, the panel's
// included: left to run, they take most of the core's time, and the figure
// would be the copy and the panel's own turns together. A turn the panel
// misses meanwhile is made up by the driver at the next vsync, and the glass
// is dark and the frame buffer black at this point.
static void measureSliceCopy() {
  static constexpr size_t RUNS = 8;
  static constexpr size_t SLICE_BYTES = (size_t)PANEL_W * LCD_BOUNCE_LINES * sizeof(uint16_t);
  static constexpr size_t SLICES_PER_FRAME = PANEL_H / LCD_BOUNCE_LINES;
  static constexpr size_t RUN_STRIDE = SLICES_PER_FRAME / RUNS;
  static_assert(PANEL_H % LCD_BOUNCE_LINES == 0, "a frame is not a whole number of slices");
  static_assert(RUN_STRIDE >= 1 && (RUNS - 1) * RUN_STRIDE < SLICES_PER_FRAME,
                "a run would read beyond the frame buffer");

  uint8_t* slice = (uint8_t*)heap_caps_malloc(SLICE_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!slice) {
    static const char msg[] = "Panel: no internal RAM for a test slice, the copy is not measured";
    Serial.println(msg);
    logSD(msg);
    return;
  }
  static portMUX_TYPE hold = portMUX_INITIALIZER_UNLOCKED;
  const uint8_t* frame = (const uint8_t*)s_fb;
  uint32_t fastest = UINT32_MAX;
  uint32_t slowest = 0;
  for (size_t run = 0; run < RUNS; run++) {
    const uint8_t* from = frame + run * RUN_STRIDE * SLICE_BYTES;
    portENTER_CRITICAL(&hold);
    const int64_t t0 = esp_timer_get_time();
    memcpy(slice, from, SLICE_BYTES);
    const int64_t t1 = esp_timer_get_time();
    portEXIT_CRITICAL(&hold);
    const uint32_t us = (uint32_t)(t1 - t0);
    if (us < fastest) fastest = us;
    if (us > slowest) slowest = us;
  }
  heap_caps_free(slice);

  char line[128];
  snprintf(line, sizeof(line),
           "Panel: a %u B slice copies from PSRAM in %u..%u us, a slice lasts %u us at %u MHz",
           (unsigned)SLICE_BYTES, (unsigned)fastest, (unsigned)slowest,
           (unsigned)LCD_SLICE_US, (unsigned)(LCD_PCLK_HZ / 1000000));
  Serial.println(line);
  logSD(line);
}

// A pin write the expander took at none of its tries, by its bit, in the
// serial and the SD log: panel power, panel reset and backlight decide
// whether there is a picture at all. The boot goes on as before.
static void expanderWrite(uint8_t bit, bool on) {
  if (io_ch32v003::write(bit, on)) return;
  char line[64];
  snprintf(line, sizeof(line), "Panel: expander write EXIO%u failed after %u tries",
           (unsigned)bit, (unsigned)io_ch32v003::WRITE_ATTEMPTS);
  Serial.println(line);
  logSD(line);
}

bool panelBegin() {
  static constexpr uint32_t LCD_POWER_SETTLE_MS = 20;
  static constexpr uint32_t LCD_RESET_SETTLE_MS = 20;
  buildScaleTables();
  // boardEarlyInit() has held the panel unpowered and in reset since
  // power-up. Its voltages first, then out of reset, then the clock.
  expanderWrite(hw_pins::EXIO_LCD_VDD, true);
  delay(LCD_POWER_SETTLE_MS);
  expanderWrite(hw_pins::EXIO_LCD_RST, true);
  delay(LCD_RESET_SETTLE_MS);
  const bool lcd_ok = rgbBegin();
  if (!lcd_ok) logSD("Panel: the RGB driver did not start, see the serial log");
  if (lcd_ok) measureSliceCopy();
  // What the panel costs in internal RAM is its two bounce buffers. The
  // figure after them is what the rest of the firmware starts with. On the
  // serial log as well: the glass may still be dark, and the SD card missing.
  char panel_line[96];
  snprintf(panel_line, sizeof(panel_line),
           "Panel: %d x %d at %u MHz (%u.%u Hz), %u bounce lines, internal heap %u free",
           PANEL_W, PANEL_H, (unsigned)(LCD_PCLK_HZ / 1000000),
           (unsigned)(LCD_FRAME_TENTHS_HZ / 10), (unsigned)(LCD_FRAME_TENTHS_HZ % 10),
           (unsigned)LCD_BOUNCE_LINES, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  Serial.println(panel_line);
  logSD(panel_line);
  touchBegin();
  // Only now, over a frame buffer that is black: no flash of whatever the
  // PSRAM held.
  if (lcd_ok) expanderWrite(hw_pins::EXIO_BACKLIGHT, true);
  // The counters, once a boot and only when write() had to try again (the
  // touch reset's two writes are in them): the evidence for or against a
  // refused write as the cause of the dark glass after the update to 912c3ff
  // (io_ch32v003.cpp, write()). Retries here and the light on confirm it; the
  // light off with no failure here rules it out.
  const uint32_t retried = io_ch32v003::retries();
  const uint32_t failed  = io_ch32v003::failures();
  if (retried || failed) {
    char line[80];
    snprintf(line, sizeof(line), "Board: CH32V003 writes needed %u retries, %u failed",
             (unsigned)retried, (unsigned)failed);
    Serial.println(line);
    logSD(line);
  }
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
