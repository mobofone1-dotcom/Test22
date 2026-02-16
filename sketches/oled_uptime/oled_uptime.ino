/*
  ESP8266 (NodeMCU 1.0 / ESP-12E) + SSD1306 128x64 OLED (I2C, meist 0x3C)
  Pins (bei dir gefunden): SDA = D5 (GPIO14), SCL = D6 (GPIO12)
  Serial: 115200
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET   -1

// I2C-Pins (bei dir gefunden)
#define I2C_SDA D5   // GPIO14
#define I2C_SCL D6   // GPIO12

#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

unsigned long lastSerialMs = 0;
unsigned long lastDisplayMs = 0;

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("OLED I2C NEW SKETCH");

  // I2C starten
  Wire.begin(I2C_SDA, I2C_SCL);

  // OLED starten
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED not found. Check wiring/pins/address.");
    while (true) { delay(1000); }
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("OLED I2C NEW SKETCH");
  display.display();
  delay(500);
}

void loop() {
  unsigned long now = millis();

  // Seriell alle 1000 ms
  if (now - lastSerialMs >= 1000) {
    lastSerialMs = now;
    Serial.println("ESP OK");
  }

  // Display alle 250 ms aktualisieren
  if (now - lastDisplayMs >= 250) {
    lastDisplayMs = now;

    unsigned long seconds = now / 1000;

    display.clearDisplay();
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.println("OLED I2C NEW SKETCH");

    display.setCursor(0, 18);
    display.print("Uptime: ");
    display.print(seconds);
    display.println(" s");

    display.setCursor(0, 34);
    display.print("I2C SDA: D5");
    display.setCursor(0, 44);
    display.print("I2C SCL: D6");
    display.setCursor(0, 54);
    display.print("Addr: 0x3C");

    display.display();
  }
}
