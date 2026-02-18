#include "ble_hr_client.h"

#include <cctype>
#include <cstring>

#if defined(ESP32)
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
static portMUX_TYPE s_hr_mux = portMUX_INITIALIZER_UNLOCKED;
#define HRM_LOCK() portENTER_CRITICAL(&s_hr_mux)
#define HRM_UNLOCK() portEXIT_CRITICAL(&s_hr_mux)
#else
#define HRM_LOCK()
#define HRM_UNLOCK()
#endif

namespace {
constexpr uint16_t kHrServiceUuid16 = 0x180D;
constexpr uint16_t kHrMeasurementCharUuid16 = 0x2A37;
constexpr uint32_t kScanDurationMs = 6000;
constexpr uint32_t kScanPauseMs = 1000;

BleHrClient* g_instance = nullptr;

NimBLEClient* g_client = nullptr;
NimBLEAdvertisedDevice* g_target = nullptr;
int g_target_priority = 0;
bool g_scan_running = false;

bool equalsIgnoreCase(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) {
    return false;
  }
  while (*a != '\0' && *b != '\0') {
    if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) {
      return false;
    }
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
}

bool containsIgnoreCase(const char* haystack, const char* needle) {
  if (haystack == nullptr || needle == nullptr || *needle == '\0') {
    return false;
  }
  const size_t hay_len = strlen(haystack);
  const size_t needle_len = strlen(needle);
  if (needle_len > hay_len) {
    return false;
  }

  for (size_t i = 0; i + needle_len <= hay_len; ++i) {
    bool match = true;
    for (size_t j = 0; j < needle_len; ++j) {
      if (std::tolower(static_cast<unsigned char>(haystack[i + j])) !=
          std::tolower(static_cast<unsigned char>(needle[j]))) {
        match = false;
        break;
      }
    }
    if (match) {
      return true;
    }
  }
  return false;
}

class HrClientCallbacks : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient* client, int reason) override {
    (void)client;
    Serial.printf("[HRM] Disconnected (reason=%d)\n", reason);
    if (g_instance != nullptr) {
      g_instance->handleDisconnected();
    }
  }
};

class HrScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* device) override {
    if (g_instance == nullptr || device == nullptr) {
      return;
    }
    g_instance->onAdvertisementSeen(device);

    const bool adv_hrs = device->isAdvertisingService(NimBLEUUID((uint16_t)kHrServiceUuid16));
    const bool has_name = device->haveName();
    const char* name = has_name ? device->getName().c_str() : "";
    int priority = 0;

    if (has_name && equalsIgnoreCase(name, "55825-1")) {
      priority = 3;
    } else if (adv_hrs) {
      priority = 2;
    } else if (has_name &&
               (containsIgnoreCase(name, "MAGENE") || containsIgnoreCase(name, "H64") ||
                containsIgnoreCase(name, "HRM"))) {
      priority = 1;
    }

    if (priority == 0) {
      return;
    }

    if (g_target == nullptr || priority > g_target_priority ||
        (priority == g_target_priority && device->getRSSI() > g_target->getRSSI())) {
      if (g_target != nullptr) {
        delete g_target;
      }
      g_target = new NimBLEAdvertisedDevice(*device);
      g_target_priority = priority;
      Serial.printf("[HRM] Candidate selected: %s name='%s' rssi=%d\n",
                    device->getAddress().toString().c_str(), has_name ? name : "",
                    device->getRSSI());
    }

    if (priority >= 3 && NimBLEDevice::getScan()->isScanning()) {
      NimBLEDevice::getScan()->stop();
    }
  }

  void onScanEnd(const NimBLEScanResults& results, int reason) override {
    (void)results;
    (void)reason;
    g_scan_running = false;
  }
};

HrClientCallbacks g_client_callbacks;
HrScanCallbacks g_scan_callbacks;

void hrNotifyCb(NimBLERemoteCharacteristic* c, uint8_t* data, size_t len, bool is_notify) {
  (void)c;
  (void)is_notify;
  if (g_instance == nullptr || data == nullptr || len < 2) {
    return;
  }
  const uint8_t flags = data[0];
  uint16_t bpm = 0;
  if ((flags & 0x01U) == 0U) {
    bpm = data[1];
  } else {
    if (len < 3) {
      return;
    }
    bpm = static_cast<uint16_t>(data[1] | (static_cast<uint16_t>(data[2]) << 8U));
  }
  g_instance->onHeartRate(bpm);
}

}  // namespace

void BleHrClient::begin() {
  g_instance = this;
  resetMetrics();
  setState(HrConnState::DISCONNECTED);
  next_scan_ms_ = millis();
  next_connect_ms_ = millis();
  backoff_ms_ = 1000;
  target_found_ = false;
  g_target_priority = 0;

#ifdef HR_DUMMY_MODE
  next_dummy_ms_ = millis() + 1000;
  setState(HrConnState::SUBSCRIBED);
  Serial.println("[HRM] HR_DUMMY_MODE active");
  return;
#else
  NimBLEDevice::init("CYD-HRM");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&g_scan_callbacks, true);
  scan->setInterval(80);
  scan->setWindow(48);
  scan->setActiveScan(true);
  Serial.println("[HRM] Using NimBLE");
#endif
}


void BleHrClient::handleDisconnected() {
  setState(HrConnState::DISCONNECTED);
  next_scan_ms_ = millis() + kScanPauseMs;
  next_connect_ms_ = next_scan_ms_;
}
void BleHrClient::setState(HrConnState new_state) {
  if (state_ == new_state) {
    return;
  }
  state_ = new_state;
  const char* name = "unknown";
  switch (new_state) {
    case HrConnState::DISCONNECTED: name = "disconnected"; break;
    case HrConnState::SCANNING: name = "scanning"; break;
    case HrConnState::CONNECTING: name = "connecting"; break;
    case HrConnState::SUBSCRIBED: name = "connected"; break;
    case HrConnState::ERROR: name = "error"; break;
  }
  Serial.printf("[HRM] State -> %s\n", name);
}

void BleHrClient::onHeartRate(uint16_t bpm) {
  HRM_LOCK();
  current_bpm_ = bpm;
  last_hr_ms_ = millis();
  if (!has_hr_) {
    min_bpm_ = bpm;
    max_bpm_ = bpm;
    has_hr_ = true;
  } else {
    if (bpm < min_bpm_) {
      min_bpm_ = bpm;
    }
    if (bpm > max_bpm_) {
      max_bpm_ = bpm;
    }
  }
  uint16_t min_bpm = min_bpm_;
  uint16_t max_bpm = max_bpm_;
  HRM_UNLOCK();

  Serial.printf("[HRM] HR: %u bpm (min %u max %u)\n", bpm, min_bpm, max_bpm);
}

void BleHrClient::loop() {
#ifdef HR_DUMMY_MODE
  const uint32_t now = millis();
  if (now >= next_dummy_ms_) {
    next_dummy_ms_ = now + 1000;
    dummy_bpm_ = static_cast<uint16_t>(dummy_bpm_ + dummy_step_);
    if (dummy_bpm_ >= 140 || dummy_bpm_ <= 60) {
      dummy_step_ = -dummy_step_;
    }
    onHeartRate(dummy_bpm_);
  }
  return;
#else
  const uint32_t now = millis();

  if (state_ != HrConnState::SUBSCRIBED && g_client != nullptr && !g_client->isConnected()) {
    g_client->disconnect();
    setState(HrConnState::DISCONNECTED);
    next_scan_ms_ = now + kScanPauseMs;
  }

  if (state_ == HrConnState::DISCONNECTED && now >= next_scan_ms_) {
    setState(HrConnState::SCANNING);
  }

  if (state_ == HrConnState::SCANNING && !g_scan_running) {
    target_found_ = false;
    g_scan_running = true;
    g_target_priority = 0;
    Serial.println("[HRM] Scanning for HR candidates...");
    NimBLEDevice::getScan()->start(kScanDurationMs / 1000, false, true);
    next_scan_ms_ = now + kScanDurationMs + kScanPauseMs;
  }

  if (g_target != nullptr && (state_ == HrConnState::SCANNING || state_ == HrConnState::DISCONNECTED)) {
    target_found_ = true;
    next_connect_ms_ = now;
    setState(HrConnState::CONNECTING);
  }

  if (state_ == HrConnState::CONNECTING && now >= next_connect_ms_) {
    Serial.printf("[HRM] Free heap before connect: %u\n", ESP.getFreeHeap());
    if (g_client == nullptr) {
      g_client = NimBLEDevice::createClient();
      g_client->setClientCallbacks(&g_client_callbacks, false);
      g_client->setConnectionParams(12, 12, 0, 100);
      g_client->setConnectTimeout(5);
    }

    bool connected = false;
    if (g_target != nullptr) {
      connected = g_client->connect(g_target);
    }

    if (!connected) {
      Serial.println("[HRM] Connect failed, scheduling backoff");
      setState(HrConnState::DISCONNECTED);
      next_connect_ms_ = now + backoff_ms_;
      next_scan_ms_ = next_connect_ms_;
      backoff_ms_ = min<uint32_t>(backoff_ms_ * 2U, 30000U);
      return;
    }

    NimBLERemoteService* service = g_client->getService(kHrServiceUuid16);
    NimBLERemoteCharacteristic* hr_char = service ? service->getCharacteristic(kHrMeasurementCharUuid16) : nullptr;
    if (service == nullptr) {
      Serial.println("[HRM] Connected to candidate but no HRS (0x180D). Disconnect.");
      g_client->disconnect();
      setState(HrConnState::DISCONNECTED);
      next_scan_ms_ = now + backoff_ms_;
      backoff_ms_ = min<uint32_t>(backoff_ms_ * 2U, 30000U);
      return;
    }

    if (hr_char == nullptr || !hr_char->canNotify()) {
      Serial.println("[HRM] Missing 0x2A37 notify characteristic");
      g_client->disconnect();
      setState(HrConnState::DISCONNECTED);
      next_scan_ms_ = now + backoff_ms_;
      backoff_ms_ = min<uint32_t>(backoff_ms_ * 2U, 30000U);
      return;
    }

    if (!hr_char->subscribe(true, hrNotifyCb)) {
      Serial.println("[HRM] Subscribe failed");
      g_client->disconnect();
      setState(HrConnState::DISCONNECTED);
      next_scan_ms_ = now + backoff_ms_;
      backoff_ms_ = min<uint32_t>(backoff_ms_ * 2U, 30000U);
      return;
    }

    Serial.println("[HRM] HRS ok, subscribed 0x2A37");
    Serial.printf("[HRM] Free heap after subscribe: %u\n", ESP.getFreeHeap());
    setState(HrConnState::SUBSCRIBED);
    backoff_ms_ = 1000;
    if (g_target != nullptr) {
      delete g_target;
      g_target = nullptr;
    }
  }

  if (state_ == HrConnState::SCANNING && now >= next_scan_ms_) {
    setState(HrConnState::DISCONNECTED);
  }
#endif
}

void BleHrClient::onAdvertisementSeen(const NimBLEAdvertisedDevice* device) {
  if (device == nullptr) {
    return;
  }
  const bool has_name = device->haveName();
  const char* name = has_name ? device->getName().c_str() : "";
  const bool adv_hrs = device->isAdvertisingService(NimBLEUUID((uint16_t)kHrServiceUuid16));

  HRM_LOCK();
  snprintf(last_seen_name_, sizeof(last_seen_name_), "%s", name);
  last_seen_rssi_ = device->getRSSI();
  HRM_UNLOCK();

  Serial.printf("[HRM] ADV %s rssi=%d conn=%d name='%s' advHRS=%d\n",
                device->getAddress().toString().c_str(), device->getRSSI(),
                device->isConnectable() ? 1 : 0, has_name ? name : "", adv_hrs ? 1 : 0);
}

void BleHrClient::resetMetrics() {
  HRM_LOCK();
  start_ms_ = millis();
  has_hr_ = false;
  current_bpm_ = 0;
  min_bpm_ = 0;
  max_bpm_ = 0;
  last_hr_ms_ = 0;
  snprintf(last_seen_name_, sizeof(last_seen_name_), "");
  last_seen_rssi_ = 0;
  HRM_UNLOCK();
  Serial.println("[HRM] Metrics reset");
}

HrSnapshot BleHrClient::snapshot() const {
  HrSnapshot snap{};
  HRM_LOCK();
  snap.current_bpm = current_bpm_;
  snap.min_bpm = min_bpm_;
  snap.max_bpm = max_bpm_;
  snap.has_hr = has_hr_;
  snap.start_ms = start_ms_;
  snap.last_hr_ms = last_hr_ms_;
  snap.state = state_;
  snprintf(snap.last_seen_name, sizeof(snap.last_seen_name), "%s", last_seen_name_);
  snap.last_seen_rssi = last_seen_rssi_;
  HRM_UNLOCK();
  return snap;
}
