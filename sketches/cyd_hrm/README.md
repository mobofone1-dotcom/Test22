# CYD HRM Sketch (ESP32 + ILI9341 + klassisches BLE)

Start-Sketch: `sketches/cyd_hrm/cyd_hrm.ino`

## Ziel
- Verbindung zu Magene H303 Pulsgurt per BLE (ohne NimBLE)
- Herzfrequenz aus `0x2A37` per Notify anzeigen
- Batterie (`0x2A19`) optional lesen und anzeigen
- Touch-Reset für Min/Max über XPT2046-Button im Header

## Benötigte Libraries
- `TFT_eSPI` (mit passendem `User_Setup` für CYD-2432S028 / ILI9341)
- ESP32 Arduino BLE (`BLEDevice.h`, `BLEScan.h`, `BLEClient.h`, `BLEAdvertisedDevice.h`)
- `XPT2046_Touchscreen`

## Wichtige Hinweise
- Diese Variante nutzt **nicht** NimBLE.
- In dieser Arduino-ESP32 BLE Umgebung wird z. B. `readValue()` als `String` verarbeitet.
  Deshalb kein `std::string` im User-Code verwenden.
- Gerätefilter läuft robust per `TARGET_MAC`.
- Scan ist passiv (`setActiveScan(false)`) mit konservativen Intervall/Window-Werten.
- Touch-Konfiguration ist für CYD `CYD_TOUCH_BASELINE_V1` ausgelegt (Pins/Kalibrierung im Sketch anpassbar).

## Erwartetes Verhalten
- UI zeigt: `Scanning...` → `Connecting...` → `Connected`
- BPM wird live aktualisiert
- Min/Max werden seit dem letzten Reset verfolgt und groß angezeigt
- `RESET`-Touchbutton setzt Min/Max auf aktuellen HR-Wert (oder `--`, falls noch kein Wert)
- Batterie wird bei Connect und danach alle 60 s gelesen (falls verfügbar)
- Nach Disconnect wird automatisch wieder gescannt und neu verbunden
