#include <NimBLEDevice.h>

#if __has_include("esp_bt.h")
#include "esp_bt.h"
#define HAS_ESP_BT_STATUS 1
#else
#define HAS_ESP_BT_STATUS 0
#endif

#define SCAN_MINIMAL_MODE 0

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

String g_unique_macs[kMaxUniqueMacs];
size_t g_unique_count = 0;
bool g_unique_storage_full = false;

int g_best_rssi = -127;
String g_best_mac;
String g_best_name;

void printControllerStatus(const char* prefix) {
#if HAS_ESP_BT_STATUS
  const esp_bt_controller_status_t st = esp_bt_controller_get_status();
  Serial.printf("%s controller_status=%d\n", prefix, static_cast<int>(st));
#else
  Serial.printf("%s controller_status=n/a\n", prefix);
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
  scan->setMaxResults(0);
  scan->clearResults();
#else
  scan->setScanCallbacks(&g_scan_cb, true);
  scan->setActiveScan(true);
  scan->setInterval(45);
  scan->setWindow(15);
  scan->setMaxResults(0);
  scan->clearResults();
#endif
}

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

  const bool started = scan->start(duration_s, false, true);
  if (!started) {
    g_scan_running = false;
    Serial.println("[SCAN] ERROR: scan->start returned false");
    printControllerStatus("[SCAN]");
    return;
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
  Serial.printf("[SCAN] heap=%u\n", ESP.getFreeHeap());

  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

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
