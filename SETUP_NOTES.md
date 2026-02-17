# SETUP_NOTES – TFT_eSPI + CYD (ESP32-2432S028R)

Diese Notizen beschreiben die nötige Konfiguration in der **TFT_eSPI**-Library für das CYD mit ILI9341.

## 1) Wo konfigurieren?

Es gibt zwei gängige Wege:

1. **Direkt in `User_Setup.h`** (einfach, aber bei Library-Updates überschrieben)
2. **Eigene Setup-Datei + `User_Setup_Select.h`** (empfohlen)

Empfohlen:
- In `TFT_eSPI/User_Setups/` eine eigene Datei anlegen, z. B. `Setup_CYD_ESP32_2432S028R.h`
- In `User_Setup_Select.h` **nur diese eine** Setup-Datei aktivieren.

---

## 2) Pflicht-Defines für ILI9341 + CYD

In deiner gewählten Setup-Datei müssen mindestens diese Defines gesetzt sein:

```cpp
#define ILI9341_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// CYD typische SPI-Pins (VSPI)
#define TFT_MOSI 13
#define TFT_MISO 12
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1   // oder auf echten RST-Pin, falls vorhanden

// Optional, wenn Backlight steuerbar ist
// #define TFT_BL   21
// #define TFT_BACKLIGHT_ON HIGH

#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY 2500000
```

> Hinweis: `TFT_WIDTH`/`TFT_HEIGHT` sind in `TFT_eSPI` oft in Portrait-Basis angegeben (240x320). Die echte Darstellung wird über `setRotation()` festgelegt.

---

## 3) Touch (XPT2046) – getrennt von TFT_eSPI

Für dieses Projekt läuft der XPT2046-Touch über die Library `XPT2046_Touchscreen` auf den vorgegebenen HSPI-Pins:

- SCK = 25
- MOSI = 32
- MISO = 39
- CS = 33
- IRQ = 36

Das wird im Sketch mit eigener SPIClass/Bus-Initialisierung gemacht, **nicht** über die TFT_eSPI-Touch-Makros.

---

## 4) Wichtiger API-Hinweis

`TFT_eSPI` bietet **kein** `getTextBounds()` wie z. B. Adafruit_GFX.

Stattdessen verwenden:
- `textWidth()`
- `fontHeight()`
- oder `drawString(..., x, y, font)` mit passender Ausrichtung/Offsets

Wenn Altcode `getTextBounds()` nutzt, muss er auf die oben genannten Methoden umgestellt werden.

---

## 5) Build-Checkliste (kurz)

- Board: ESP32 Dev Module (oder kompatibel)
- PSRAM: Disabled
- Library-Setup korrekt ausgewählt (`User_Setup_Select.h`)
- `TFT_eSPI`, `XPT2046_Touchscreen`, `lvgl` installiert
- lokale `lv_conf.h` vorhanden (in diesem Projekt enthalten)

Damit ist die Basis für einen erfolgreichen Build auf dem ESP32 CYD gesetzt.
