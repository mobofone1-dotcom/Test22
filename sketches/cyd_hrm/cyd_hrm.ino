#include <Arduino.h>
#include <TFT_eSPI.h>

#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include <BLEScan.h>

// Zielgerät
static const char* TARGET_MAC = "c1:3c:43:e3:5b:3e";
static const char* TARGET_NAME = "55825-1";  // nur Anzeige

// Standard BLE UUIDs (Bluetooth SIG)
static BLEUUID HR_SERVICE((uint16_t)0x180D);
static BLEUUID HR_MEAS_CHAR((uint16_t)0x2A37);
static BLEUUID BAT_SERVICE((uint16_t)0x180F);
static BLEUUID BAT_LEVEL_CHAR((uint16_t)0x2A19);

TFT_eSPI tft;

BLEScan* pScan = nullptr;
BLEAdvertisedDevice* foundDevice = nullptr;
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pHRChar = nullptr;

bool doConnect = false;
bool connected = false;

volatile uint16_t g_hr = 0;
volatile int g_batt = -1;
volatile bool g_newHR = false;

uint32_t lastScanStartMs = 0;
uint32_t lastUiMs = 0;
uint32_t lastBattReadMs = 0;

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
  }
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

static void uiHeader() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);

  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("CYD HRM", 10, 8);

  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(String("Name: ") + TARGET_NAME, 10, 34);
  tft.drawString(String("MAC:  ") + TARGET_MAC, 10, 48);

  tft.drawFastHLine(0, 68, tft.width(), TFT_DARKGREY);
}

static void uiStatus(const String& s) {
  tft.fillRect(0, 72, tft.width(), 22, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(s, 10, 74);
}

static void uiValues(uint16_t hr, int batt) {
  tft.fillRect(0, 100, tft.width(), 80, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("HR", 10, 105);

  tft.setTextSize(6);
  tft.drawString(String(hr), 10, 132);

  tft.setTextSize(2);
  tft.drawString("bpm", 190, 152);

  tft.fillRect(0, 190, tft.width(), 30, TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  if (batt >= 0) {
    tft.drawString(String("Battery: ") + batt + "%", 10, 195);
  } else {
    tft.drawString("Battery: --", 10, 195);
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
    if (pClient->isConnected()) {
      pClient->disconnect();
    }
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
  tft.setRotation(1);  // ggf. 0/2/3 je nach Layout
  uiHeader();
  uiStatus("Init...");

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
    if (connected && upd) uiValues(g_hr, g_batt);
  }

  delay(10);
}
