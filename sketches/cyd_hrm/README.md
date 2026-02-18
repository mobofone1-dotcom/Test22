# CYD HRM Sketch (BLE Brustgurt)

Start-Sketch: `sketches/cyd_hrm/cyd_hrm.ino`

## Benötigte Libraries
- TFT_eSPI
- XPT2046_Touchscreen
- lvgl (v9)
- NimBLE-Arduino (bevorzugt)
  - Fallback im Code: ESP32 BLE Arduino (`BLEDevice.h`), falls NimBLE nicht verfügbar ist.

## Hinweise
- Optionaler Testbetrieb ohne BLE: `#define HR_DUMMY_MODE` in `cyd_hrm.ino` aktivieren.
- Touch-Button **Reset** setzt Laufzeit sowie MIN/MAX zurück.
- MVP: RR-Interval und Battery Service werden nicht ausgewertet.
