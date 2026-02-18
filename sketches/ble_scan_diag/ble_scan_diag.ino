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
uint32_t g_round_number = 0;
uint32_t g_next_scan_ms = 0;
uint32_t g_scan_start_ms = 0;
uint32_t g_last_heartbeat_second = 0;

class DiagScanCallbacks : public NimBLEScanCallbacks {
 public:
  void onResult(const NimBLEAdvertisedDevice* device) override {
    (void)device;
  }
};

DiagScanCallbacks g_scan_callbacks;

std::string buildServiceList(const NimBLEAdvertisedDevice* device) {
  std::string service_uuids;
  const int svc_count = device->getServiceUUIDCount();
  for (int i = 0; i < svc_count; ++i) {
    if (!service_uuids.empty()) {
      service_uuids += ",";
    }
    service_uuids += device->getServiceUUID(i).toString();
  }
  return service_uuids;
}

void printAndSummarize(const NimBLEScanResults& results) {
  g_stats.clear();

  const uint32_t found = results.getCount();
  Serial.printf("[SCAN] found=%lu\n", static_cast<unsigned long>(found));

  for (uint32_t i = 0; i < found; ++i) {
    const NimBLEAdvertisedDevice* device = results.getDevice(i);
    if (device == nullptr) {
      continue;
    }

    const std::string addr = device->getAddress().toString();
    const int rssi = device->getRSSI();
    const bool has_name = device->haveName();
    const std::string name = has_name ? device->getName() : "";
    const std::string service_uuids = buildServiceList(device);

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

    Serial.printf("[SCAN] addr=%s rssi=%d name='%s' svcs=%s\n", addr.c_str(), rssi,
                  name.c_str(), service_uuids.c_str());
  }

  if (g_stats.best.valid) {
    Serial.printf("[SCAN] summary: advs=%lu unique=%u named=%lu best=%d\n",
                  static_cast<unsigned long>(g_stats.count_total_advs),
                  static_cast<unsigned int>(g_stats.unique_addresses.size()),
                  static_cast<unsigned long>(g_stats.count_with_name), g_stats.best.rssi);
  } else {
    Serial.printf("[SCAN] summary: advs=%lu unique=%u named=%lu best=n/a\n",
                  static_cast<unsigned long>(g_stats.count_total_advs),
                  static_cast<unsigned int>(g_stats.unique_addresses.size()),
                  static_cast<unsigned long>(g_stats.count_with_name));
  }
}

void startRound() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  g_round_number++;
  g_scan_running = true;
  g_scan_start_ms = millis();
  g_last_heartbeat_second = 0;

  Serial.printf("[SCAN] round %lu start (15s)\n", static_cast<unsigned long>(g_round_number));
  scan->start(kScanDurationSeconds, false, true);
}

void maybePrintHeartbeat(uint32_t now_ms) {
  if (!g_scan_running) {
    return;
  }

  const uint32_t elapsed_s = (now_ms - g_scan_start_ms) / 1000;
  while (g_last_heartbeat_second < elapsed_s && g_last_heartbeat_second < kScanDurationSeconds) {
    g_last_heartbeat_second++;
    Serial.printf("[SCAN] t=%lus\n", static_cast<unsigned long>(g_last_heartbeat_second));
  }
}

void finishRound() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (scan->isScanning()) {
    scan->stop();
  }

  const NimBLEScanResults& results = scan->getResults();
  printAndSummarize(results);
  scan->clearResults();

  g_scan_running = false;
  g_next_scan_ms = millis() + kScanPauseMs;
}

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
  scan->setInterval(45);
  scan->setWindow(15);
  scan->setDuplicateFilter(false);

  g_next_scan_ms = millis();
}

void loop() {
  const uint32_t now = millis();

  if (!g_scan_running && now >= g_next_scan_ms) {
    startRound();
  }

  maybePrintHeartbeat(now);

  if (g_scan_running) {
    NimBLEScan* scan = NimBLEDevice::getScan();
    const uint32_t elapsed_ms = now - g_scan_start_ms;

    if (!scan->isScanning() || elapsed_ms >= (kScanDurationSeconds * 1000UL + 1000UL)) {
      while (g_last_heartbeat_second < kScanDurationSeconds) {
        g_last_heartbeat_second++;
        Serial.printf("[SCAN] t=%lus\n", static_cast<unsigned long>(g_last_heartbeat_second));
      }
      finishRound();
    }
  }

  delay(20);
}
