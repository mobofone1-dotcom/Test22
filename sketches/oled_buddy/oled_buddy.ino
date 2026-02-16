/*
  OLED Buddy für NodeMCU 1.0 (ESP-12E)
  - FLASH-Button (GPIO0 / D3) wechselt den Anzeigemodus
  - I2C: SDA = D5 (GPIO14), SCL = D6 (GPIO12)
  - OLED-Adresse: 0x3C
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET   -1

#define I2C_SDA D5
#define I2C_SCL D6
#define OLED_ADDR 0x3C

#define FLASH_BTN D3   // NodeMCU FLASH Button (GPIO0), aktiv LOW

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int mode = 0;
const int modeCount = 3;

bool lastBtnState = HIGH;
unsigned long lastDebounceMs = 0;
const unsigned long debounceDelayMs = 30;

void drawMode0(unsigned long nowMs) {
  display.setCursor(0, 0);
  display.println("OLED Buddy");
  display.println("Mode 0: Uptime");
  display.println();
  display.print("Uptime: ");
  display.print(nowMs / 1000);
  display.println(" s");
}

void drawMode1() {
  display.setCursor(0, 0);
  display.println("OLED Buddy");
  display.println("Mode 1: Pins");
  display.println();
  display.println("SDA: D5 (GPIO14)");
  display.println("SCL: D6 (GPIO12)");
  display.println("Addr: 0x3C");
}

void drawMode2(unsigned long nowMs) {
  display.setCursor(0, 0);
  display.println("OLED Buddy");
  display.println("Mode 2: Heartbeat");
  display.println();

  const bool beat = ((nowMs / 400) % 2) == 0;
  display.print("Status: ");
  display.println(beat ? "<3" : "   ");
  display.println("FLASH -> naechster");
  display.println("Mode");
}

void setup() {
  pinMode(FLASH_BTN, INPUT_PULLUP);

  Serial.begin(115200);
  delay(50);

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED nicht gefunden. Verkabelung/Pins/Adresse pruefen.");
    while (true) {
      delay(1000);
    }
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("OLED Buddy bereit");
  display.println("FLASH = Mode");
  display.display();
  delay(800);
}

void loop() {
  unsigned long nowMs = millis();

  // Taster mit einfacher Entprellung
  bool currentBtnState = digitalRead(FLASH_BTN);
  if (currentBtnState != lastBtnState) {
    lastDebounceMs = nowMs;
    lastBtnState = currentBtnState;
  }

  if ((nowMs - lastDebounceMs) > debounceDelayMs && currentBtnState == LOW) {
    mode = (mode + 1) % modeCount;
    Serial.print("Neuer Modus: ");
    Serial.println(mode);

    // Warten bis Taste losgelassen wird
    while (digitalRead(FLASH_BTN) == LOW) {
      delay(10);
    }
    delay(20);
  }

  display.clearDisplay();
  display.setTextSize(1);

  if (mode == 0) {
    drawMode0(nowMs);
  } else if (mode == 1) {
    drawMode1();
  } else {
    drawMode2(nowMs);
  }

  display.display();
  delay(80);
}
