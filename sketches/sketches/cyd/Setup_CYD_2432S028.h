
#pragma once

// === Driver ===
#define ILI9341_DRIVER

// === Display size (CYD ILI9341 is 240x320) ===
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// === CYD SPI pins (ESP32) ===
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  -1

// Optional backlight pin (only if you use it in your sketch)
#define TFT_BL 21

// === SPI speed ===
#define SPI_FREQUENCY 40000000
