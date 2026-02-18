/*
  CYD (ESP32 + ILI9341) – Magene H303 BLE HRM -> Anzeige auf TFT + Touch Min/Max Reset
  - Filter per MAC (robust, auch ohne Gerätenamen)
  - Connect -> HR Notify (0x180D/0x2A37) -> Battery Read (0x180F/0x2A19)
  - Anzeige auf CYD via TFT_eSPI
  - Touchscreen (XPT2046) Button "RESET": setzt Min/Max seit Reset zurück
  - UI Design:
    * Fokus: HR sehr groß + Min/Max groß sichtbar + Reset-Button
    * Rest (Name/MAC/Battery/Status) klein
  - uiStatus() klein, überschreibt Name/MAC nicht.

  Voraussetzungen:
  - Arduino-ESP32 Core
  - TFT_eSPI ist für dein CYD korrekt konfiguriert (User_Setup passend für ILI9341/CYD)
  - XPT2046_Touchscreen Library installiert

  Touch-Konfig basiert auf CYD_TOUCH_BASELINE_V1:
  HSPI: CS=33, SCK=25, MISO=39, MOSI=32
  SWAP_XY=1, INVERT_X=0, INVERT_Y=1
  MINX=171 MAXX=3868 MINY=254 MAXY=3794
*/

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>

#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include <BLEScan.h>

#include <XPT2046_Touchscreen.h>

// ======= Zielgerät =======
static const char* TARGET_MAC = "c1:3c:43:e3:5b:3e";  // aus deinem Log
static const char* TARGET_NAME = "55825-1";            // optional nur zur Anzeige

// ======= BLE UUIDs (Bluetooth SIG Standard) =======
static BLEUUID HR_SERVICE((uint16_t)0x180D);
static BLEUUID HR_MEAS_CHAR((uint16_t)0x2A37);

static BLEUUID BAT_SERVICE((uint16_t)0x180F);
static BLEUUID BAT_LEVEL_CHAR((uint16_t)0x2A19);

// ======= TFT =======
TFT_eSPI tft;

// ======= TOUCH (CYD_TOUCH_BASELINE_V1) =======
static const int XPT_CS = 33;
static const int XPT_SCK = 25;
static const int XPT_MISO = 39;
static const int XPT_MOSI = 32;

#define TS_SWAP_XY 1
#define TS_INVERT_X 0
#define TS_INVERT_Y 1
#define TS_MINX 171
#define TS_MAXX 3868
#define TS_MINY 254
#define TS_MAXY 3794

static const int Z_MIN = 5;       // robust
static const int TS_SAMPLES = 2;  // Mittelung

SPIClass touchSPI(HSPI);
XPT2046_Touchscreen ts(XPT_CS);
bool lastPressed = false;

// Button-Rechteck (Header)
struct Rect {
  int x, y, w, h;
};
Rect btnMinMax = {0, 0, 0, 0};

// ======= Status/Values =======
BLEScan* pScan = nullptr;
BLEAdvertisedDevice* foundDevice = nullptr;

BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pHRChar = nullptr;

bool doConnect = false;
bool connected = false;

volatile uint16_t g_hr = 0;
volatile int g_batt = -1;
volatile bool g_newHR = false;

// Min/Max seit Reset (Touch-Reset)
volatile uint16_t g_hrMin = 0;
volatile uint16_t g_hrMax = 0;
volatile bool g_haveHR = false;

uint32_t lastScanStartMs = 0;
uint32_t lastUiMs = 0;
uint32_t lastBattReadMs = 0;

// ======= Display Caching gegen Flackern =======
static uint16_t lastDispHR = 0xFFFF;
static int lastDispBatt = -999;
static uint16_t lastDispMin = 0xFFFF;
static uint16_t lastDispMax = 0xFFFF;

static uint16_t parseHeartRate(const uint8_t* data, size_t len) {
  if (len < 2) return 0;
  uint8_t flags = data[0];
  bool hr16 = (flags & 0x01) != 0;
  if (!hr16) return data[1];
  if (len < 3) return 0;
  return (uint16_t)data[1] | ((uint16_t)data[2] << 8);
}

static void hrNotifyCallback(BLERemoteCharacteristic*, uint8_t* pData, size_t length, bool) {
  uint16_t hr = parseHeartRate(pData, length);
  if (hr) {
    g_hr = hr;
    g_newHR = true;

    // Min/Max pflegen
    if (!g_haveHR) {
      g_haveHR = true;
      g_hrMin = hr;
      g_hrMax = hr;
    } else {
      if (hr < g_hrMin) g_hrMin = hr;
      if (hr > g_hrMax) g_hrMax = hr;
    }
  }
}

class MyClientCallbacks : public BLEClientCallbacks {
  void onConnect(BLEClient*) override { connected = true; }

  void onDisconnect(BLEClient*) override {
    connected = false;
    g_batt = -1;
    // Display-Caches zurücksetzen, damit nach Reconnect sauber neu gezeichnet wird
    lastDispHR = 0xFFFF;
    lastDispBatt = -999;
    lastDispMin = 0xFFFF;
    lastDispMax = 0xFFFF;
  }
};

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice adv) override {
    // MAC-Filter (robust)
    String mac = adv.getAddress().toString().c_str();
    if (!mac.equalsIgnoreCase(TARGET_MAC)) return;

    if (foundDevice) {
      delete foundDevice;
      foundDevice = nullptr;
    }
    foundDevice = new BLEAdvertisedDevice(adv);

    doConnect = true;
    pScan->stop();
  }
};

// ======= UI LAYOUT =======
// Bildschirm (Rotation 1): 320x240
// Header: y 0..44
// Status: y 46..64 (klein)
// HR Big: y 68..170
// Footer: y 175..239 (Min/Max groß + Battery klein)

static void uiHeader() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);

  // Titel klein links
  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("HRM", 10, 6);

  // Name/MAC klein
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(String("Name: ") + TARGET_NAME, 10, 28);
  tft.drawString(String("MAC:  ") + TARGET_MAC, 10, 40);

  // Reset-Button rechts oben
  btnMinMax.w = 128;
  btnMinMax.h = 32;
  btnMinMax.x = tft.width() - btnMinMax.w - 10;
  btnMinMax.y = 6;

  tft.fillRoundRect(btnMinMax.x, btnMinMax.y, btnMinMax.w, btnMinMax.h, 8, TFT_DARKGREY);
  tft.drawRoundRect(btnMinMax.x, btnMinMax.y, btnMinMax.w, btnMinMax.h, 8, TFT_WHITE);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.drawString("RESET", btnMinMax.x + 26, btnMinMax.y + 8);

  // Trennlinien
  tft.drawFastHLine(0, 66, tft.width(), TFT_DARKGREY);
  tft.drawFastHLine(0, 174, tft.width(), TFT_DARKGREY);

  // Caches resetten
  lastDispHR = 0xFFFF;
  lastDispBatt = -999;
  lastDispMin = 0xFFFF;
  lastDispMax = 0xFFFF;
}

static void uiStatus(const String& s) {
  // kleiner Statusbalken, überschreibt Name/MAC nicht
  tft.fillRect(0, 46, tft.width(), 18, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);

  // gleiche kleine Schrift wie Name/MAC
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(s, 10, 50);
}

// Weniger Flackern: nur Bereiche redrawen, wenn sich Werte ändern
static void uiValues(uint16_t hr, int batt) {
  tft.setTextDatum(TL_DATUM);

  // Statische Labels 1x zeichnen
  static bool drawnStatic = false;
  if (!drawnStatic) {
    // HR-Panel säubern
    tft.fillRect(0, 68, tft.width(), 106, TFT_BLACK);

    // Footer säubern
    tft.fillRect(0, 176, tft.width(), 64, TFT_BLACK);

    // kleine HR Markierung
    tft.setTextSize(2);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("HR", 10, 72);

    drawnStatic = true;
  }

  // HR sehr groß, nur wenn geändert
  if (hr != lastDispHR) {
    lastDispHR = hr;

    tft.fillRect(0, 90, tft.width(), 80, TFT_BLACK);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(8);
    String s = String(hr);

    int approxCharW = 6 * 8;
    int x = (tft.width() - (int)s.length() * approxCharW) / 2;
    if (x < 10) x = 10;

    tft.drawString(s, x, 98);

    tft.setTextSize(2);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("bpm", tft.width() - 55, 148);
  }

  // Min/Max groß, nur wenn geändert
  uint16_t curMin = g_haveHR ? (uint16_t)g_hrMin : 0xFFFF;
  uint16_t curMax = g_haveHR ? (uint16_t)g_hrMax : 0xFFFF;

  if (curMin != lastDispMin || curMax != lastDispMax) {
    lastDispMin = curMin;
    lastDispMax = curMax;

    tft.fillRect(0, 178, tft.width(), 36, TFT_BLACK);

    tft.setTextSize(3);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);

    if (g_haveHR) {
      tft.drawString(String("Min: ") + (uint16_t)g_hrMin, 10, 184);
      tft.drawString(String("Max: ") + (uint16_t)g_hrMax, 170, 184);
    } else {
      tft.drawString("Min: --", 10, 184);
      tft.drawString("Max: --", 170, 184);
    }
  }

  // Battery klein, nur wenn geändert
  if (batt != lastDispBatt) {
    lastDispBatt = batt;

    tft.fillRect(0, 216, tft.width(), 22, TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    if (batt >= 0)
      tft.drawString(String("Batt: ") + batt + "%", 10, 218);
    else
      tft.drawString("Batt: --", 10, 218);
  }
}

static void startScan() {
  doConnect = false;
  pScan->clearResults();
  pScan->start(5, false);
  lastScanStartMs = millis();
}

static void readBatteryOnce() {
  if (!pClient || !connected) return;

  BLERemoteService* batSvc = pClient->getService(BAT_SERVICE);
  if (!batSvc) return;

  BLERemoteCharacteristic* bat = batSvc->getCharacteristic(BAT_LEVEL_CHAR);
  if (!bat || !bat->canRead()) return;

  // In deiner Umgebung liefert readValue() Arduino String
  String v = bat->readValue();
  if (v.length() >= 1) {
    g_batt = (uint8_t)v[0];
  }
}

static bool connectAndSubscribe() {
  if (!foundDevice) return false;

  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallbacks());

  if (!pClient->connect(foundDevice)) return false;

  BLERemoteService* hrSvc = pClient->getService(HR_SERVICE);
  if (!hrSvc) {
    pClient->disconnect();
    return false;
  }

  pHRChar = hrSvc->getCharacteristic(HR_MEAS_CHAR);
  if (!pHRChar || !pHRChar->canNotify()) {
    pClient->disconnect();
    return false;
  }

  pHRChar->registerForNotify(hrNotifyCallback);

  delay(50);
  readBatteryOnce();
  lastBattReadMs = millis();

  return true;
}

// Touch lesen + mappen auf Screen-Koordinaten (Rotation-korrekt + Spiegel-Fix)
static bool readTouchXY(int& sx, int& sy) {
  if (!ts.touched()) return false;

  long ax = 0, ay = 0, az = 0;
  int got = 0;

  for (int i = 0; i < TS_SAMPLES; i++) {
    if (!ts.touched()) break;
    TS_Point p = ts.getPoint();  // raw
    ax += p.x;
    ay += p.y;
    az += p.z;
    got++;
    delay(1);
  }
  if (got == 0) return false;

  int rx = (int)(ax / got);
  int ry = (int)(ay / got);
  int rz = (int)(az / got);
  if (rz < Z_MIN) return false;

#if TS_SWAP_XY
  int t = rx;
  rx = ry;
  ry = t;
#endif
#if TS_INVERT_X
  rx = TS_MAXX - (rx - TS_MINX);
#endif
#if TS_INVERT_Y
  ry = TS_MAXY - (ry - TS_MINY);
#endif

  if (rx < TS_MINX) rx = TS_MINX;
  if (rx > TS_MAXX) rx = TS_MAXX;
  if (ry < TS_MINY) ry = TS_MINY;
  if (ry > TS_MAXY) ry = TS_MAXY;

  const int W0 = 240;
  const int H0 = 320;
  int x0 = map(rx, TS_MINX, TS_MAXX, 0, W0 - 1);
  int y0 = map(ry, TS_MINY, TS_MAXY, 0, H0 - 1);

  uint8_t rot = tft.getRotation() & 3;

  if (rot == 0) {
    sx = x0;
    sy = y0;
  } else if (rot == 1) {
    sx = y0;
    sy = (W0 - 1) - x0;
  } else if (rot == 2) {
    sx = (W0 - 1) - x0;
    sy = (H0 - 1) - y0;
  } else {
    sx = (H0 - 1) - y0;
    sy = x0;
  }

  // Spiegel-Fix
  sx = (tft.width() - 1) - sx;
  sy = (tft.height() - 1) - sy;

  if (sx < 0) sx = 0;
  if (sy < 0) sy = 0;
  if (sx >= tft.width()) sx = tft.width() - 1;
  if (sy >= tft.height()) sy = tft.height() - 1;

  return true;
}

static inline bool hit(const Rect& r, int x, int y) {
  return (x >= r.x && x < (r.x + r.w) && y >= r.y && y < (r.y + r.h));
}

static void resetMinMax() {
  if (g_haveHR && g_hr > 0) {
    g_hrMin = g_hr;
    g_hrMax = g_hr;
  } else {
    g_haveHR = false;
    g_hrMin = 0;
    g_hrMax = 0;
  }

  lastDispMin = 0xFFFF;
  lastDispMax = 0xFFFF;
}

void setup() {
  Serial.begin(115200);
  delay(50);

  tft.init();
  tft.setRotation(1);
  uiHeader();
  uiStatus("Init...");

  touchSPI.begin(XPT_SCK, XPT_MISO, XPT_MOSI, XPT_CS);
  ts.begin(touchSPI);

  BLEDevice::init("CYD_HRM");
  pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pScan->setActiveScan(false);
  pScan->setInterval(600);
  pScan->setWindow(60);

  uiStatus("Scanning...");
  startScan();
}

void loop() {
  if (doConnect && !connected) {
    doConnect = false;
    uiStatus("Connecting...");
    if (connectAndSubscribe()) {
      uiStatus("Connected");
      uiValues(g_hr, g_batt);
    } else {
      uiStatus("Connect failed");
      delay(800);
      uiStatus("Scanning...");
      startScan();
    }
  }

  if (!connected) {
    if (millis() - lastScanStartMs > 8000) {
      uiStatus("Scanning...");
      startScan();
    }
  }

  if (connected && (millis() - lastBattReadMs > 60000)) {
    readBatteryOnce();
    lastBattReadMs = millis();
  }

  if (millis() - lastUiMs > 250 || g_newHR) {
    lastUiMs = millis();
    bool upd = g_newHR;
    g_newHR = false;

    if (connected) {
      if (upd) uiValues(g_hr, g_batt);
    } else {
      uiValues(g_hr, g_batt);
    }
  }

  int tx, ty;
  bool pressed = readTouchXY(tx, ty);
  if (pressed && !lastPressed) {
    if (hit(btnMinMax, tx, ty)) {
      resetMinMax();
      uiHeader();
      uiStatus(connected ? "Min/Max reset" : "Reset (not connected)");
      uiValues(g_hr, g_batt);
    }
  }
  lastPressed = pressed;

  delay(10);
}
