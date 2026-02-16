#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED Groesse
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// OLED Reset nicht benutzt
#define OLED_RESET -1

// I2C Pins fuer ESP8266
#define OLED_SDA D5
#define OLED_SCL D6

// OLED Adresse
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

unsigned long lastSerialMs = 0;

void setup() {
  Serial.begin(115200);

  // I2C starten
  Wire.begin(OLED_SDA, OLED_SCL);

  // OLED starten
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED Fehler");
    while (true) {
      delay(100);
    }
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
}

void loop() {
  // Uptime in Sekunden
  unsigned long uptimeSec = millis() / 1000;

  // OLED Text ausgeben
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println("OK");

  display.setTextSize(1);
  display.setCursor(0, 28);
  display.print("Uptime: ");
  display.print(uptimeSec);
  display.println(" s");
  display.display();

  // Jede Sekunde Serial Meldung
  if (millis() - lastSerialMs >= 1000) {
    lastSerialMs += 1000;
    Serial.println("ESP OK");
  }

  delay(50);
}
