# NodeMCU 1.0 (ESP-12E / ESP8266) + OLED (SSD1306)

## Confirmed setup
- Board: NodeMCU 1.0 (ESP-12E / ESP8266)
- OLED I2C address: `0x3C`
- Working I2C pins:
  - SDA = D5 (GPIO14)
  - SCL = D6 (GPIO12)
- Arduino IDE Serial Monitor baud: `115200`

## Troubleshooting
- Garbled serial text usually means the baud rate is wrong.
- `no I2C devices found` usually means SDA/SCL are wrong, or GND/3V3 is missing.

- Ergänze dort (falls noch nicht drin) diese harten Fakten, die ihr gemessen habt:

Board: NodeMCU 1.0 (ESP-12E / ESP8266)

OLED I2C-Adresse: 0x3C

Funktionierende Pins: SDA = D5 (GPIO14), SCL = D6 (GPIO12)

Serial Monitor: 115200 Baud

Hinweis: „Wirres Zeichenzeug“ im Monitor = meist falsche Baudrate
