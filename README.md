# ESP32 CYD (ESP32-2432S028R) – Arduino IDE Setup

Dieses Projekt ist für ein **ESP32 CYD (Cheap Yellow Display, ESP32-2432S028R)** mit:
- **TFT:** ILI9341 (320x240) über `TFT_eSPI`
- **Touch:** XPT2046 ("Touch B") über HSPI
- **LVGL v9** mit RAM-sparsamer Konfiguration (**ohne PSRAM**)

---

## 1) Arduino IDE vorbereiten

1. **Arduino IDE 2.x** installieren.
2. Unter **Datei → Voreinstellungen → Zusätzliche Boardverwalter-URLs** hinzufügen:
   - `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. Unter **Werkzeuge → Board → Boardverwalter** nach **esp32** suchen und installieren (Espressif).
4. Board wählen:
   - **ESP32 Dev Module** (oder ein kompatibles ESP32-WROOM-Profil)

Empfohlene Optionen:
- Flash Size: **4MB**
- PSRAM: **Disabled**
- Partition Scheme: **Default 4MB with spiffs** (oder ähnlich)
- Upload Speed: **921600** (bei Problemen 460800)

---

## 2) Bibliotheken installieren

Über **Sketch → Bibliothek einbinden → Bibliotheken verwalten** installieren:

- `TFT_eSPI` (Bodmer)
- `XPT2046_Touchscreen` (Paul Stoffregen)
- `lvgl` (v9)

> Hinweis: Nach Änderungen an `TFT_eSPI`-Setupdateien die IDE einmal neu starten.

---

## 3) Pinout

### Display (CYD Standard / TFT_eSPI)
Für das CYD wird das ILI9341-TFT mit den üblichen Board-Pins betrieben. Die konkreten `#define`s setzt du in `TFT_eSPI` (siehe `SETUP_NOTES.md`).

### Touch (XPT2046 Touch B, HSPI)
Folgende Pins verwenden:
- `TOUCH_SCK = 25`
- `TOUCH_MOSI = 32`
- `TOUCH_MISO = 39`
- `TOUCH_CS = 33`
- `TOUCH_IRQ = 36`

Diese Touch-Pins sind explizit von der Aufgabenstellung vorgegeben.

---

## 4) LVGL v9 Konfiguration

Im Projekt liegt eine `lv_conf.h`, die für **LVGL v9 + Arduino** gedacht ist und RAM-sparsame Defaults setzt (passend für **PSRAM=0**).

Wichtig:
- `lv_conf.h` muss im Include-Pfad sichtbar sein (Projektordner ist dafür üblich).
- Keine großen Draw-Buffer wählen.
- Keine unnötigen LVGL-Features aktivieren.

---

## 5) Rotation & Touch-Kalibrierung

- TFT-Rotation typischerweise mit `tft.setRotation(0..3)` testen.
- Touch-Achsen können je nach Rotation gespiegelt oder vertauscht sein.
- Falls Touch-Koordinaten nicht passen:
  - X/Y tauschen
  - Achsen invertieren
  - Rohwerte auf Displayauflösung mappen (`0..319`, `0..239`)

Praxis-Tipp:
1. Erst Display-Rotation festlegen.
2. Dann Touch auf diese Rotation anpassen.
3. Erst danach UI-Layout finalisieren.

---

## 6) Zielstatus

Mit den Einstellungen aus diesem Repository (`README.md`, `lv_conf.h`, `SETUP_NOTES.md`) ist das Projekt so vorbereitet, dass ein Arduino-Build für CYD/ESP32 ohne zusätzliche Testsketche erfolgreich konfigurierbar ist.
