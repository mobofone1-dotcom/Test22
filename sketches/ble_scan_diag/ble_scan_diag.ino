#include <NimBLEDevice.h>

#include <string>
#include <vector>

namespace {
constexpr uint32_t kScanDurationSeconds = 15;
constexpr uint32_t kScanPauseMs = 2000;

struct BestDevice {
  bool valid = false;
  std::string addr;
  std::string name;
  int rssi = -127;
};

struct ScanStats {
  uint32_t count_total_advs = 0;
  uint32_t count_with_name = 0;
  std::vector<std::string> unique_addresses;
  BestDevice best;

  void clear() {
    count_total_advs = 0;
    count_with_name = 0;
    unique_addresses.clear();
    best = BestDevice{};
  }

  bool hasAddress(const std::string& addr) const {
    for (const auto& item : unique_addresses) {
      if (item == addr) {
        return true;
      }
    }
    return false;
  }
};

ScanStats g_stats;
bool g_scan_running = false;
uint32_t g_next_scan_ms = 0;

class DiagScanCallbacks : public NimBLEScanCallbacks {
 public:
  void onResult(const NimBLEAdvertisedDevice* device) override {
    if (device == nullptr) {
      return;
    }

    const std::string addr = device->getAddress().toString();
    const int rssi = device->getRSSI();
    const bool connectable = device->isConnectable();
    const bool has_name = device->haveName();
    const std::string name = has_name ? device->getName() : "";

    std::string service_uuids;
    const int svc_count = device->getServiceUUIDCount();
    for (int i = 0; i < svc_count; ++i) {
      if (!service_uuids.empty()) {
        service_uuids += ",";
      }
      service_uuids += device->getServiceUUID(i).toString();
    }

    g_stats.count_total_advs++;
    if (has_name) {
      g_stats.count_with_name++;
    }
    if (!g_stats.hasAddress(addr)) {
      g_stats.unique_addresses.push_back(addr);
    }
    if (!g_stats.best.valid || rssi > g_stats.best.rssi) {
      g_stats.best.valid = true;
      g_stats.best.addr = addr;
      g_stats.best.rssi = rssi;
      g_stats.best.name = name;
    }

    Serial.printf("[SCAN] addr=%s rssi=%d connectable=%s name='%s' svcs=%s\n", addr.c_str(),
                  rssi, connectable ? "true" : "false", name.c_str(), service_uuids.c_str());
  }

  void onScanEnd(const NimBLEScanResults& results, int reason) override {
    (void)results;
    (void)reason;

    if (g_stats.best.valid) {
      Serial.printf("[SCAN] summary: advs=%lu unique=%u named=%lu best=%d addr=%s name='%s'\n",
                    static_cast<unsigned long>(g_stats.count_total_advs),
                    static_cast<unsigned int>(g_stats.unique_addresses.size()),
                    static_cast<unsigned long>(g_stats.count_with_name), g_stats.best.rssi,
                    g_stats.best.addr.c_str(), g_stats.best.name.c_str());
    } else {
      Serial.printf("[SCAN] summary: advs=%lu unique=%u named=%lu best=n/a addr= name=''\n",
                    static_cast<unsigned long>(g_stats.count_total_advs),
                    static_cast<unsigned int>(g_stats.unique_addresses.size()),
                    static_cast<unsigned long>(g_stats.count_with_name));
    }

    g_scan_running = false;
    g_next_scan_ms = millis() + kScanPauseMs;
  }
};

DiagScanCallbacks g_scan_callbacks;

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[SCAN] BLE scan diagnostics starting");

  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&g_scan_callbacks, false);
  scan->setActiveScan(true);

  g_next_scan_ms = millis();
}

void loop() {
  const uint32_t now = millis();
  if (!g_scan_running && now >= g_next_scan_ms) {
    g_stats.clear();
    g_scan_running = true;
    Serial.printf("[SCAN] starting round: %lu seconds\n",
                  static_cast<unsigned long>(kScanDurationSeconds));
    NimBLEDevice::getScan()->start(kScanDurationSeconds, false, true);
  }

  delay(10);
}
