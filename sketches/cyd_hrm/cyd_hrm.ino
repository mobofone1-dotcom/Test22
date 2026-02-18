#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include <BLEScan.h>

// Zielgerät (Magene H303)
static const char* TARGET_MAC = "c1:3c:43:e3:5b:3e";
static const char* TARGET_NAME = "Magene H303";  // nur Anzeige

// Standard BLE UUIDs (Bluetooth SIG)
static BLEUUID HR_SERVICE((uint16_t)0x180D);
static BLEUUID HR_MEAS_CHAR((uint16_t)0x2A37);
static BLEUUID BAT_SERVICE((uint16_t)0x180F);
static BLEUUID BAT_LEVEL_CHAR((uint16_t)0x2A19);

// CYD Touch (HSPI)
static const int TS_CS = 33;
static const int TS_IRQ = -1;
static const int TS_SCK = 25;
static const int TS_MISO = 39;
static const int TS_MOSI = 32;

static const bool TS_SWAP_XY = true;
static const bool TS_INVERT_X = false;
static const bool TS_INVERT_Y = true;
static const int TS_MINX = 171;
static const int TS_MAXX = 3868;
static const int TS_MINY = 254;
static const int TS_MAXY = 3794;
static const int TS_Z_MIN = 5;

TFT_eSPI tft;
SPIClass hspi(HSPI);
XPT2046_Touchscreen ts(TS_CS, TS_IRQ);

BLEScan* pScan = nullptr;
BLEAdvertisedDevice* foundDevice = nullptr;
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pHRChar = nullptr;

bool doConnect = false;
bool connected = false;

volatile uint16_t g_hr = 0;
volatile bool g_newHR = false;
int g_batt = -1;

uint16_t hrMin = 0;
uint16_t hrMax = 0;

uint32_t lastScanStartMs = 0;
uint32_t lastUiMs = 0;
uint32_t lastBattReadMs = 0;
uint32_t lastTouchMs = 0;

const int BTN_X = 220;
const int BTN_Y = 8;
const int BTN_W = 92;
const int BTN_H = 36;

struct UiCache {
  String status;
  uint16_t hr = 0xFFFF;
  int batt = -999;
  uint16_t min = 0xFFFF;
  uint16_t max = 0xFFFF;
} ui;

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
  if (!hr) return;

  g_hr = hr;
  g_newHR = true;
}

class MyClientCallbacks : public BLEClientCallbacks {
  void onConnect(BLEClient*) override { connected = true; }

  void onDisconnect(BLEClient*) override {
    connected = false;
    g_batt = -1;
    pHRChar = nullptr;
  }
};

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice adv) override {
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

static void drawResetButton() {
  tft.fillRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 8, TFT_DARKGREY);
  tft.drawRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 8, TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setTextSize(2);
  tft.drawString("RESET", BTN_X + BTN_W / 2, BTN_Y + BTN_H / 2);
}

static void uiHeader() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);

  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("CYD BLE HRM", 10, 10);

  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(String("Name: ") + TARGET_NAME, 10, 34);
  tft.drawString(String("MAC:  ") + TARGET_MAC, 10, 48);

  drawResetButton();
  tft.drawFastHLine(0, 68, tft.width(), TFT_DARKGREY);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("HR", 10, 106);
  tft.drawString("bpm", 260, 146);

  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("MIN", 10, 196);
  tft.drawString("MAX", 170, 196);
}

static void uiStatus(const String& s) {
  if (ui.status == s) return;
  ui.status = s;

  tft.fillRect(0, 72, tft.width(), 22, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(s, 10, 74);
}

static void uiDrawHR(uint16_t hr) {
  if (ui.hr == hr) return;
  ui.hr = hr;

  tft.fillRect(0, 128, 250, 60, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(7);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(hr > 0 ? String(hr) : String("--"), 10, 126);
}

static void uiDrawBattery(int batt) {
  if (ui.batt == batt) return;
  ui.batt = batt;

  tft.fillRect(0, 220, tft.width(), 20, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(TFT_SKYBLUE, TFT_BLACK);
  if (batt >= 0) {
    tft.drawString(String("Battery: ") + batt + "%", 10, 220);
  } else {
    tft.drawString("Battery: --", 10, 220);
  }
}

static void uiDrawMinMax(uint16_t minHr, uint16_t maxHr) {
  if (ui.min == minHr && ui.max == maxHr) return;
  ui.min = minHr;
  ui.max = maxHr;

  tft.fillRect(10, 168, 130, 26, TFT_BLACK);
  tft.fillRect(170, 168, 130, 26, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(4);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString(minHr > 0 ? String(minHr) : String("--"), 10, 166);
  tft.drawString(maxHr > 0 ? String(maxHr) : String("--"), 170, 166);
}

static void resetMinMax() {
  hrMin = 0;
  hrMax = 0;
  uiDrawMinMax(hrMin, hrMax);
}

static bool mapAndReadTouch(int& sx, int& sy) {
  if (!ts.touched()) return false;

  TS_Point p = ts.getPoint();
  if (p.z < TS_Z_MIN) return false;

  int tx = p.x;
  int ty = p.y;

  if (TS_SWAP_XY) {
    int tmp = tx;
    tx = ty;
    ty = tmp;
  }
  if (TS_INVERT_X) tx = TS_MAXX - (tx - TS_MINX);
  if (TS_INVERT_Y) ty = TS_MAXY - (ty - TS_MINY);

  tx = constrain(tx, TS_MINX, TS_MAXX);
  ty = constrain(ty, TS_MINY, TS_MAXY);

  sx = map(tx, TS_MINX, TS_MAXX, 0, tft.width() - 1);
  sy = map(ty, TS_MINY, TS_MAXY, 0, tft.height() - 1);

  // Spiegel-Fix nach Rotation
  sx = tft.width() - 1 - sx;
  sy = tft.height() - 1 - sy;

  return true;
}

static void pollTouch() {
  int sx = 0;
  int sy = 0;
  if (!mapAndReadTouch(sx, sy)) return;

  uint32_t now = millis();
  if (now - lastTouchMs < 250) return;
  lastTouchMs = now;

  bool inReset = (sx >= BTN_X && sx < (BTN_X + BTN_W) && sy >= BTN_Y && sy < (BTN_Y + BTN_H));
  if (inReset) {
    resetMinMax();
    uiStatus("Min/Max reset");
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

  String v = bat->readValue();
  if (v.length() >= 1) g_batt = (uint8_t)v[0];
}

static bool connectAndSubscribe() {
  if (!foundDevice) return false;

  if (pClient) {
    if (pClient->isConnected()) pClient->disconnect();
    delete pClient;
    pClient = nullptr;
  }

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

void setup() {
  Serial.begin(115200);
  delay(50);

  tft.init();
  tft.setRotation(1);

  hspi.begin(TS_SCK, TS_MISO, TS_MOSI, TS_CS);
  ts.begin(hspi);
  ts.setRotation(1);

  uiHeader();
  uiStatus("Init...");
  uiDrawHR(0);
  uiDrawMinMax(0, 0);
  uiDrawBattery(-1);

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
  pollTouch();

  if (doConnect && !connected) {
    doConnect = false;
    uiStatus("Connecting...");
    if (connectAndSubscribe()) {
      uiStatus("Connected");
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

    bool hasNewHR = g_newHR;
    g_newHR = false;

    uiDrawBattery(g_batt);

    if (connected && hasNewHR) {
      uint16_t hr = g_hr;
      uiDrawHR(hr);

      if (hrMin == 0 || hr < hrMin) hrMin = hr;
      if (hrMax == 0 || hr > hrMax) hrMax = hr;
      uiDrawMinMax(hrMin, hrMax);
      uiStatus("Connected");
    }

    if (!connected) {
      uiDrawHR(0);
      uiDrawBattery(-1);
    }
  }

  delay(10);
}
