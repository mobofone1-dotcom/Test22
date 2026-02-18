#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>

#include "ble_hr_client.h"
#include "ui_hr.h"

// #define HR_DUMMY_MODE

namespace {
constexpr uint16_t TFT_HOR_RES = 320;
constexpr uint16_t TFT_VER_RES = 240;

constexpr int TOUCH_SCK = 25;
constexpr int TOUCH_MOSI = 32;
constexpr int TOUCH_MISO = 39;
constexpr int TOUCH_CS = 33;
constexpr int TOUCH_IRQ = 36;

TFT_eSPI tft;
SPIClass touch_spi(HSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

lv_display_t* g_display = nullptr;
lv_indev_t* g_indev = nullptr;

constexpr uint32_t kUiUpdateMs = 200;
uint32_t g_next_ui_update = 0;

BleHrClient g_hr;

lv_color_t g_buf1[TFT_HOR_RES * 20];
lv_color_t g_buf2[TFT_HOR_RES * 20];

void myFlushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  uint32_t w = static_cast<uint32_t>(area->x2 - area->x1 + 1);
  uint32_t h = static_cast<uint32_t>(area->y2 - area->y1 + 1);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushPixels(reinterpret_cast<uint16_t*>(px_map), w * h, true);
  tft.endWrite();
  lv_display_flush_ready(disp);
}

void myTouchRead(lv_indev_t* indev, lv_indev_data_t* data) {
  (void)indev;
  if (!touch.touched()) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  TS_Point p = touch.getPoint();
  // Basic mapping for rotation=1 (320x240). Fine-tune if needed.
  int16_t x = map(p.x, 200, 3850, 0, TFT_HOR_RES - 1);
  int16_t y = map(p.y, 240, 3850, 0, TFT_VER_RES - 1);
  if (x < 0) x = 0;
  if (x >= TFT_HOR_RES) x = TFT_HOR_RES - 1;
  if (y < 0) y = 0;
  if (y >= TFT_VER_RES) y = TFT_VER_RES - 1;

  data->state = LV_INDEV_STATE_PRESSED;
  data->point.x = x;
  data->point.y = y;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n[CYD-HRM] Boot");

  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  touch_spi.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(touch_spi);
  touch.setRotation(1);

  lv_init();
  g_display = lv_display_create(TFT_HOR_RES, TFT_VER_RES);
  lv_display_set_flush_cb(g_display, myFlushCb);
  lv_display_set_buffers(g_display, g_buf1, g_buf2, sizeof(g_buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);

  g_indev = lv_indev_create();
  lv_indev_set_type(g_indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(g_indev, myTouchRead);

  g_hr.begin();
  ui_hr::init(&g_hr);

  g_next_ui_update = millis();
}

void loop() {
  const uint32_t now = millis();

  g_hr.loop();

  if (now >= g_next_ui_update) {
    g_next_ui_update = now + kUiUpdateMs;
    ui_hr::refresh(g_hr.snapshot(), now);
  }

  lv_timer_handler();
  delay(5);
}

