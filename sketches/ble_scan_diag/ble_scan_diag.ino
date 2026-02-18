#include <NimBLEDevice.h>

#if __has_include("esp_bt.h")
#include "esp_bt.h"
#define HAS_ESP_BT_H 1
#else
#define HAS_ESP_BT_H 0
#endif

#if __has_include("esp_bt_main.h")
#include "esp_bt_main.h"
#define HAS_ESP_BT_MAIN_H 1
#else
#define HAS_ESP_BT_MAIN_H 0
#endif

#if __has_include("nvs_flash.h")
#include "nvs_flash.h"
#define HAS_NVS_FLASH_H 1
#else
#define HAS_NVS_FLASH_H 0
#endif

#if __has_include("BLEDevice.h")
#include "BLEDevice.h"
#define HAS_ESP32_BLE_ARDUINO 1
#else
#define HAS_ESP32_BLE_ARDUINO 0
#endif

#define SCAN_MINIMAL_MODE 0
#define USE_ESP32_BLE_ARDUINO_FALLBACK 1

namespace {
constexpr uint32_t kScanDurationSeconds = 15;
constexpr uint32_t kScanPauseMs = 2000;
constexpr uint32_t kScanWaitTimeoutMs = (kScanDurationSeconds + 5) * 1000;
constexpr size_t kMaxUniqueMacs = 200;

volatile uint32_t g_advs_total = 0;
volatile uint32_t g_named = 0;
volatile bool g_scan_running = false;
volatile uint32_t g_scan_start_ms = 0;
volatile uint32_t g_last_heartbeat_second = 0;
volatile bool g_scan_end_seen = false;
volatile int g_scan_end_reason = 0;
volatile uint32_t g_last_found_count = 0;

struct ScanStartParams {
  uint16_t interval = 0;
  uint16_t window = 0;
  bool active = false;
  bool duplicate_filter = false;
  uint32_t duration_s = 0;
  bool continuation = false;
  bool restart = false;
};

ScanStartParams g_scan_params;

String g_unique_macs[kMaxUniqueMacs];
size_t g_unique_count = 0;
bool g_unique_storage_full = false;

int g_best_rssi = -127;
String g_best_mac;
String g_best_name;

int getControllerStatus() {
#if HAS_ESP_BT_H
  return static_cast<int>(esp_bt_controller_get_status());
#else
  return -1;
#endif
}

int getBluedroidStatus() {
#if HAS_ESP_BT_MAIN_H
  return static_cast<int>(esp_bluedroid_get_status());
#else
  return -1;
#endif
}

int getBtStartedStatus() {
#if HAS_ESP_BT_H
  return btStarted() ? 1 : 0;
#else
  return -1;
#endif
}

void printControllerStatus(const char* prefix) {
#if HAS_ESP_BT_H
  const esp_bt_controller_status_t st = esp_bt_controller_get_status();
  Serial.printf("%s controller_status=%d\n", prefix, static_cast<int>(st));
#else
  Serial.printf("%s controller_status=n/a\n", prefix);
#endif
}

void printRuntimeStatus(const char* prefix) {
  Serial.printf("%s btStarted=%d\n", prefix, getBtStartedStatus());
  Serial.printf("%s esp_bt_controller_get_status=%d\n", prefix, getControllerStatus());
  Serial.printf("%s esp_bluedroid_get_status=%d\n", prefix, getBluedroidStatus());
}

void printPlatformInfo() {
  Serial.printf("[SCAN] sdk=%s\n", ESP.getSdkVersion());
  Serial.printf("[SCAN] chip=%s rev=%d\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("[SCAN] heap=%u psram=%u\n", ESP.getFreeHeap(), ESP.getFreePsram());
}

void printCoreBuildWarning() {
  Serial.println(
      "[SCAN] WARN: Bluetooth stack in this core build might be disabled. Check Tools -> "
      "Core Debug Level and board package version; ensure Bluetooth is enabled in this "
      "ESP32 core build.");
}

bool controllerIsEnabled() {
#if HAS_ESP_BT_H
  return esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED;
#else
  return false;
#endif
}

bool tryEnableControllerForDiagnostics() {
#if HAS_ESP_BT_H
  Serial.println("[SCAN] controller not ready, attempting explicit BLE controller init/enable");

#if HAS_NVS_FLASH_H
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    Serial.printf("[SCAN] nvs_flash_init=%d, erase+retry\n", err);
    (void)nvs_flash_erase();
    err = nvs_flash_init();
  }
  Serial.printf("[SCAN] nvs_flash_init final=%d\n", err);
#else
  Serial.println("[SCAN] nvs_flash.h not available, cannot verify/init NVS here");
#endif

  esp_err_t release_ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  Serial.printf("[SCAN] esp_bt_controller_mem_release(CLASSIC)=%d\n", release_ret);

  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  esp_err_t init_ret = esp_bt_controller_init(&bt_cfg);
  if (init_ret == ESP_ERR_INVALID_STATE) {
    Serial.printf("[SCAN] esp_bt_controller_init=%d (already initialized)\n", init_ret);
  } else {
    Serial.printf("[SCAN] esp_bt_controller_init=%d\n", init_ret);
  }

  esp_err_t enable_ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (enable_ret == ESP_ERR_INVALID_STATE) {
    Serial.printf("[SCAN] esp_bt_controller_enable(BLE)=%d (already enabled?)\n", enable_ret);
  } else {
    Serial.printf("[SCAN] esp_bt_controller_enable(BLE)=%d\n", enable_ret);
  }

  printRuntimeStatus("[SCAN]");
  const bool enabled = controllerIsEnabled();
  if (!enabled) {
    printCoreBuildWarning();
  }
  return enabled;
#else
  Serial.println("[SCAN] esp_bt.h not available, cannot manually control BLE controller");
  return false;
#endif
}

void printNimBLEApiInfo() {
#ifdef NIMBLE_CPP_VERSION
  Serial.printf("[SCAN] NimBLE CPP version: %s\n", NIMBLE_CPP_VERSION);
#elif defined(NIMBLE_VERSION)
  Serial.printf("[SCAN] NimBLE version macro: %s\n", NIMBLE_VERSION);
#else
  Serial.println("[SCAN] NimBLE version macro: unknown");
#endif
  Serial.println("[SCAN] Using NimBLEScanCallbacks + start(duration, continue, restart)");
}

void resetRoundStats() {
  g_advs_total = 0;
  g_named = 0;
  g_unique_count = 0;
  g_unique_storage_full = false;
  g_best_rssi = -127;
  g_best_mac = "";
  g_best_name = "";
  g_scan_end_seen = false;
  g_scan_end_reason = 0;
  g_last_found_count = 0;
}

bool rememberUnique(const String& mac) {
  for (size_t i = 0; i < g_unique_count; ++i) {
    if (g_unique_macs[i] == mac) {
      return false;
    }
  }

  if (g_unique_count < kMaxUniqueMacs) {
    g_unique_macs[g_unique_count++] = mac;
    return true;
  }

  g_unique_storage_full = true;
  return false;
}

String buildServiceList(const NimBLEAdvertisedDevice* dev) {
  if (dev == nullptr) {
    return "n/a";
  }

  const int svc_count = dev->getServiceUUIDCount();
  if (svc_count <= 0) {
    return "n/a";
  }

  String out;
  for (int i = 0; i < svc_count; ++i) {
    if (i > 0) {
      out += ",";
    }
    out += String(dev->getServiceUUID(i).toString().c_str());
  }
  return out;
}

class DiagScanCallbacks : public NimBLEScanCallbacks {
 public:
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    if (dev == nullptr) {
      return;
    }

    g_advs_total++;

    const int rssi = dev->getRSSI();
    const String mac = String(dev->getAddress().toString().c_str());
    const bool has_name = dev->haveName();
    const String name = has_name ? String(dev->getName().c_str()) : "";
    const String svc = buildServiceList(dev);

    if (has_name) {
      g_named++;
    }

    rememberUnique(mac);

    if (g_best_mac.isEmpty() || rssi > g_best_rssi) {
      g_best_rssi = rssi;
      g_best_mac = mac;
      g_best_name = name;
    }

    Serial.printf("[SCAN] adv rssi=%d mac=%s name=\"%s\" svc=%s\n", rssi, mac.c_str(),
                  name.c_str(), svc.c_str());
  }

  void onScanEnd(const NimBLEScanResults& results, int reason) override {
    g_last_found_count = results.getCount();
    g_scan_end_reason = reason;
    g_scan_end_seen = true;
    g_scan_running = false;
  }
};

DiagScanCallbacks g_scan_cb;

void heartbeatTask(void*) {
  while (true) {
    if (g_scan_running) {
      const uint32_t elapsed_s = (millis() - g_scan_start_ms) / 1000;
      while (g_last_heartbeat_second < elapsed_s &&
             g_last_heartbeat_second < kScanDurationSeconds) {
        g_last_heartbeat_second++;
        Serial.printf("[SCAN] t=%lus\n", static_cast<unsigned long>(g_last_heartbeat_second));
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void configureScan(NimBLEScan* scan) {
#if SCAN_MINIMAL_MODE
  scan->setScanCallbacks(&g_scan_cb, true);
  scan->setActiveScan(false);
  scan->setInterval(45);
  scan->setWindow(15);
  scan->setDuplicateFilter(true);
  scan->setMaxResults(0);
  scan->clearResults();

  g_scan_params.interval = 45;
  g_scan_params.window = 15;
  g_scan_params.active = false;
  g_scan_params.duplicate_filter = true;
#else
  scan->setScanCallbacks(&g_scan_cb, true);
  scan->setActiveScan(true);
  scan->setInterval(45);
  scan->setWindow(15);
  scan->setDuplicateFilter(true);
  scan->setMaxResults(0);
  scan->clearResults();

  g_scan_params.interval = 45;
  g_scan_params.window = 15;
  g_scan_params.active = true;
  g_scan_params.duplicate_filter = true;
#endif
}

void logScanStartParams() {
  Serial.printf(
      "[SCAN] start params: interval=%u window=%u active=%d duplicate_filter=%d duration=%lu "
      "continue=%d restart=%d\n",
      static_cast<unsigned int>(g_scan_params.interval),
      static_cast<unsigned int>(g_scan_params.window), g_scan_params.active,
      g_scan_params.duplicate_filter, static_cast<unsigned long>(g_scan_params.duration_s),
      g_scan_params.continuation, g_scan_params.restart);
}

#if USE_ESP32_BLE_ARDUINO_FALLBACK && HAS_ESP32_BLE_ARDUINO
void runEsp32BleArduinoFallback() {
  Serial.println("[SCAN] fallback: trying ESP32 BLE Arduino scan for 5s");
  BLEDevice::init("");
  BLEScan* fallback_scan = BLEDevice::getScan();
  if (fallback_scan == nullptr) {
    Serial.println("[SCAN] fallback: BLEDevice::getScan() returned null");
    Serial.println("[SCAN] BLE stack not functional in this build");
    return;
  }

  fallback_scan->setActiveScan(false);
  BLEScanResults results = fallback_scan->start(5, false);
  Serial.printf("[SCAN] fallback: devices=%d\n", results.getCount());
  if (results.getCount() <= 0) {
    Serial.println("[SCAN] BLE stack not functional in this build");
  }
}
#else
void runEsp32BleArduinoFallback() {
  Serial.println("[SCAN] fallback: <BLEDevice.h> not available in this core build");
  Serial.println("[SCAN] BLE stack not functional in this build");
}
#endif

void printSummary() {
  const String best_id = g_best_name.isEmpty() ? g_best_mac : g_best_name;

  Serial.printf("[SCAN] found=%u reason=%d\n", static_cast<unsigned int>(g_last_found_count),
                g_scan_end_reason);
  if (!best_id.isEmpty()) {
    Serial.printf("[SCAN] summary: advs=%lu unique=%u named=%lu best=%s/%d\n",
                  static_cast<unsigned long>(g_advs_total),
                  static_cast<unsigned int>(g_unique_count),
                  static_cast<unsigned long>(g_named), best_id.c_str(), g_best_rssi);
  } else {
    Serial.printf("[SCAN] summary: advs=%lu unique=%u named=%lu best=n/a\n",
                  static_cast<unsigned long>(g_advs_total),
                  static_cast<unsigned int>(g_unique_count),
                  static_cast<unsigned long>(g_named));
  }

  if (g_unique_storage_full) {
    Serial.printf("[SCAN] note: unique MAC storage capped at %u\n",
                  static_cast<unsigned int>(kMaxUniqueMacs));
  }

  if (!g_scan_end_seen) {
    Serial.println("[SCAN] ERROR: scan end callback was not called (API/runtime issue).");
  }

  if (g_advs_total == 0) {
    Serial.println(
        "[SCAN] ERROR: No advertisements received. Possible BLE controller/board/core "
        "configuration issue.");
    Serial.printf("[SCAN] heap=%u\n", ESP.getFreeHeap());
    printControllerStatus("[SCAN]");
  }
}

void runScanRound() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (scan == nullptr) {
    Serial.println("[SCAN] FATAL: BLE stack not available / init failed");
    while (true) {
      delay(1000);
    }
  }

  resetRoundStats();
  configureScan(scan);

  g_scan_running = true;
  g_scan_start_ms = millis();
  g_last_heartbeat_second = 0;

#if SCAN_MINIMAL_MODE
  const uint32_t duration_s = 10;
#else
  const uint32_t duration_s = kScanDurationSeconds;
#endif
  g_scan_params.duration_s = duration_s;
  g_scan_params.continuation = false;
  g_scan_params.restart = true;

  logScanStartParams();

  bool started = scan->start(duration_s, g_scan_params.continuation, g_scan_params.restart);
  if (!started) {
    g_scan_running = false;
    Serial.printf("[SCAN] FAIL start(): btStarted=%d ctrl=%d blue=%d heap=%u\n",
                  getBtStartedStatus(), getControllerStatus(), getBluedroidStatus(),
                  ESP.getFreeHeap());
    Serial.println("[SCAN] retry: NimBLEDevice::deinit(true) -> init(\"\")");
    NimBLEDevice::deinit(true);
    delay(200);
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    scan = NimBLEDevice::getScan();
    if (scan == nullptr) {
      Serial.println("[SCAN] ERROR: retry failed, scan object is null");
      runEsp32BleArduinoFallback();
      return;
    }

    configureScan(scan);
    logScanStartParams();
    started = scan->start(duration_s, g_scan_params.continuation, g_scan_params.restart);
    if (!started) {
      Serial.printf("[SCAN] FAIL start() retry: btStarted=%d ctrl=%d blue=%d heap=%u\n",
                    getBtStartedStatus(), getControllerStatus(), getBluedroidStatus(),
                    ESP.getFreeHeap());
      Serial.println("[SCAN] END: scan start failed after one retry");
      runEsp32BleArduinoFallback();
      return;
    }
  }

  const uint32_t wait_started = millis();
  while (g_scan_running && (millis() - wait_started) < kScanWaitTimeoutMs) {
    delay(20);
  }

  if (g_scan_running) {
    Serial.println("[SCAN] ERROR: scan timeout waiting for onScanEnd");
    scan->stop();
    g_scan_running = false;
  }

  while (g_last_heartbeat_second < duration_s) {
    g_last_heartbeat_second++;
    Serial.printf("[SCAN] t=%lus\n", static_cast<unsigned long>(g_last_heartbeat_second));
  }

  printSummary();
  scan->clearResults();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("[SCAN] BLE scan diagnostics starting");
  printPlatformInfo();

  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  printRuntimeStatus("[SCAN]");
  if (getBtStartedStatus() == 0 || !controllerIsEnabled()) {
    (void)tryEnableControllerForDiagnostics();
  }

#if HAS_ESP_BT_MAIN_H
  if (getBluedroidStatus() == ESP_BLUEDROID_STATUS_ENABLED) {
    Serial.println("[SCAN] note: Bluedroid host is enabled; this may conflict with NimBLE host");
  }
#endif

  NimBLEScan* scan = NimBLEDevice::getScan();
  if (scan == nullptr) {
    Serial.println("[SCAN] FATAL: BLE stack not available / init failed");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("[SCAN] NimBLE init OK");
  printNimBLEApiInfo();
  printControllerStatus("[SCAN]");

  xTaskCreatePinnedToCore(heartbeatTask, "scan_heartbeat", 3072, nullptr, 1, nullptr, 1);
}

void loop() {
  runScanRound();
  delay(kScanPauseMs);
}
