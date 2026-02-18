# CYD BLE HRM (Magene H303)

Sketch: `sketches/cyd_hrm/cyd_hrm.ino`

## Ziel
- CYD (ESP32 + ILI9341 + XPT2046) liest einen BLE-Pulsgurt (Magene H303) aus.
- Anzeige auf dem CYD: HR sehr groß, Min/Max groß, Touch-Button `RESET` setzt Min/Max zurück.
- Robuste BLE-Umsetzung mit ESP32 BLE Arduino (`BLEDevice`), **ohne NimBLE**.

## Warum der Ansatz so umgesetzt ist
1. NimBLE wurde bewusst nicht verwendet, weil das Verhalten mit dem klassischen ESP32 BLE Stack stabil war.
2. In manchen Arduino-ESP32/BLE-Umgebungen liefern APIs wie `readValue()`/`getName()` Arduino `String`.
   Deshalb wird im Sketch konsequent mit Arduino `String` gearbeitet und nicht mit `std::string`.
3. Das Finden des Gurts erfolgt über `TARGET_MAC` (Name kann leer/inkonsistent sein).
4. UI-Bereiche werden nur bei Änderung neu gezeichnet, um Flackern zu vermeiden.
5. Touch-Mapping ist für CYD kalibriert, inklusive Spiegel-Fix nach Rotation.

## BLE UUIDs
- Heart Rate Service: `0x180D`
- Heart Rate Measurement (Notify): `0x2A37`
- Battery Service: `0x180F`
- Battery Level (Read): `0x2A19`

## Touch-Konfiguration (CYD Baseline)
- HSPI: `CS=33`, `SCK=25`, `MISO=39`, `MOSI=32`
- Calibration:
  - `TS_SWAP_XY=1`
  - `TS_INVERT_X=0`
  - `TS_INVERT_Y=1`
  - `TS_MINX=171 TS_MAXX=3868 TS_MINY=254 TS_MAXY=3794`
- `TS_Z_MIN=5`
- Spiegel-Fix nach Rotation: `sx=w-1-sx`, `sy=h-1-sy`

## Benötigte Libraries
- `TFT_eSPI` (mit passendem CYD / ILI9341 Setup)
- `XPT2046_Touchscreen`
- ESP32 BLE Arduino (`BLEDevice.h`, `BLEScan.h`, `BLEClient.h`, `BLEAdvertisedDevice.h`)

## Bedienung
- Start: `Scanning...` → `Connecting...` → `Connected`
- HR kommt via Notify und wird groß angezeigt.
- Min/Max werden ab erstem HR geführt.
- Touch auf `RESET` setzt Min/Max auf leer (`--`).
- Batterie wird bei Connect und danach alle 60 s gelesen (falls verfügbar).
