# BLE All-in-One Diagnose Sketch

Dieser Sketch fasst die bisherigen BLE-Diagnose-Stufen in eine einzige serielle State-Machine zusammen.

## Dateien

- `ble_diag_allinone.ino`

## Arduino IDE Einstellungen (wie im Repository)

Nutze die gleichen Basis-Einstellungen wie in der Root-`README.md` von Test22:

- Board: **ESP32 Dev Module** (bzw. kompatibles ESP32-WROOM-Profil)
- Flash Size: **4MB**
- PSRAM: **Disabled**
- Partition Scheme: **Default 4MB with spiffs**
- Upload Speed: **921600** (bei Bedarf 460800)
- Bibliothek: **NimBLE-Arduino** verfügbar

## Nutzung

1. Sketch `sketches/ble_diag_allinone/ble_diag_allinone.ino` öffnen.
2. Auf ESP32 flashen.
3. Seriellen Monitor auf **115200 Baud** öffnen.
4. Den automatischen Ablauf beobachten:
   - `S0_BOOT_INFO`
   - `S1_BT_BRINGUP`
   - `S2_SCAN_ROUND` (R1/R2/R3 in Schleife)
   - `S3_FILTER_TARGET`
   - `S4_CONNECT_HRM`
   - `S5_WAIT_NOTIFY`
   - bei hartem Fehler `S_ERR_HARDFAIL` (mit anschließendem Neustart der Diagnose)

## Log-Interpretation

- Wenn **S1 scheitert** (auffällige Returncodes), ist häufig BT im Core/Build deaktiviert oder nicht sauber initialisierbar.
- Wenn in **S2 `start()=0`** (auch nach Retry), ist der Controller/Host sehr wahrscheinlich nicht aktiv.
- Wenn in **S2 `advs=0`** bleibt, deutet das auf Funk-/Scan-/Board-Thema hin (Umgebung, Antenne, Board-Konfig).
- Wenn **S4 Connect fehlschlägt**, liegt es oft an Pairing/Range/falschem Zielgerät.

## Hinweise

- Keine Display-/LVGL-/TFT-/Touch-Abhängigkeiten.
- Alle Diagnosen laufen ausschließlich über `Serial` mit Prefixen wie `[ALL][Sx]`.
