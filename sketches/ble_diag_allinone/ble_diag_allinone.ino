#include <NimBLEDevice.h>

#include <algorithm>
#include <type_traits>

#if __has_include("esp_bt.h")
#include "esp_bt.h"
#define ALL_HAS_ESP_BT_H 1
#else
#define ALL_HAS_ESP_BT_H 0
#endif

#if __has_include("esp_bt_main.h")
#include "esp_bt_main.h"
#define ALL_HAS_ESP_BT_MAIN_H 1
#else
#define ALL_HAS_ESP_BT_MAIN_H 0
#endif

#if __has_include("nvs_flash.h")
#include "nvs_flash.h"
#define ALL_HAS_NVS_FLASH_H 1
#else
#define ALL_HAS_NVS_FLASH_H 0
#endif

namespace {

constexpr uint16_t kHrServiceUuid16 = 0x180D;
constexpr uint16_t kHrMeasurementCharUuid16 = 0x2A37;
constexpr uint32_t kRoundDurationSeconds = 15;
constexpr uint32_t kNotifyWaitMs = 10000;
constexpr uint32_t kStateTickMs = 50;
constexpr size_t kMaxUniqueMacs = 200;

enum State {
  S0_BOOT_INFO,
  S1_BT_BRINGUP,
  S2_SCAN_ROUND,
  S3_FILTER_TARGET,
  S4_CONNECT_HRM,
  S5_WAIT_NOTIFY,
  S_ERR_HARDFAIL,
};

struct RoundCfg {
  bool active;
  bool dup;
  uint16_t interval;
  uint16_t window;
};

constexpr RoundCfg kRoundPlan[] = {
    {true, false, 80, 48},
    {true, true, 80, 48},
    {false, false, 80, 48},
};

struct Candidate {
  bool valid = false;
  String mac;
  String name;
  int rssi = -127;
  bool has_service = false;
};

struct ScanStats {
  uint32_t advs = 0;
  uint32_t named = 0;
  uint32_t unique = 0;
  bool unique_full = false;
  int best_rssi = -127;
  String best_mac;
  String best_name;
};

template <typename T, typename = void>
struct HasSetScanCallbacks : std::false_type {};

template <typename T>
struct HasSetScanCallbacks<
    T, std::void_t<decltype(std::declval<T&>().setScanCallbacks(nullptr, true))>>
    : std::true_type {};

template <typename T, typename = void>
struct HasSetAdvertisedCallbacks : std::false_type {};

template <typename T>
struct HasSetAdvertisedCallbacks<
    T, std::void_t<decltype(std::declval<T&>().setAdvertisedDeviceCallbacks(nullptr, true))>>
    : std::true_type {};

template <typename T, typename = void>
struct HasAdvertisingService : std::false_type {};

template <typename T>
struct HasAdvertisingService<
    T, std::void_t<decltype(std::declval<const T&>().isAdvertisingService(
           NimBLEUUID((uint16_t)kHrServiceUuid16)))>> : std::true_type {};

int getBtStartedStatus() {
#if ALL_HAS_ESP_BT_H
  return btStarted() ? 1 : 0;
#else
  return -1;
#endif
}

int getControllerStatus() {
#if ALL_HAS_ESP_BT_H
  return static_cast<int>(esp_bt_controller_get_status());
#else
  return -1;
#endif
}

int getBluedroidStatus() {
#if ALL_HAS_ESP_BT_MAIN_H
  return static_cast<int>(esp_bluedroid_get_status());
#else
  return -1;
#endif
}

State g_state = S0_BOOT_INFO;
uint32_t g_state_started_ms = 0;
bool g_state_entry = true;
uint32_t g_round_index = 0;
uint32_t g_round_counter = 0;
uint32_t g_last_heartbeat_s = 0;
bool g_scan_running = false;
bool g_scan_end_seen = false;
int g_scan_end_reason = 0;
String g_unique_macs[kMaxUniqueMacs];
ScanStats g_stats;
Candidate g_target_by_name;
Candidate g_target_by_service;
Candidate g_best_candidate;
bool g_start_retry_done = false;
NimBLEClient* g_client = nullptr;
NimBLERemoteCharacteristic* g_hr_char = nullptr;
bool g_notify_received = false;
uint32_t g_notify_started_ms = 0;

const char* stateName(State s) {
  switch (s) {
    case S0_BOOT_INFO:
      return "S0";
    case S1_BT_BRINGUP:
      return "S1";
    case S2_SCAN_ROUND:
      return "S2";
    case S3_FILTER_TARGET:
      return "S3";
    case S4_CONNECT_HRM:
      return "S4";
    case S5_WAIT_NOTIFY:
      return "S5";
    case S_ERR_HARDFAIL:
      return "FAIL";
  }
  return "?";
}

void transition(State next) {
  Serial.printf("[ALL][%s] -> [%s]\n", stateName(g_state), stateName(next));
  g_state = next;
  g_state_started_ms = millis();
  g_state_entry = true;
}

bool rememberUnique(const String& mac) {
  for (uint32_t i = 0; i < g_stats.unique; ++i) {
    if (g_unique_macs[i] == mac) {
      return false;
    }
  }

  if (g_stats.unique < kMaxUniqueMacs) {
    g_unique_macs[g_stats.unique++] = mac;
    return true;
  }

  g_stats.unique_full = true;
  return false;
}

bool deviceHasHrService(const NimBLEAdvertisedDevice* dev) {
  if (dev == nullptr) {
    return false;
  }

  if constexpr (HasAdvertisingService<NimBLEAdvertisedDevice>::value) {
    return dev->isAdvertisingService(NimBLEUUID((uint16_t)kHrServiceUuid16));
  }

  return false;
}

void resetScanStats() {
  g_stats = ScanStats{};
  g_target_by_name = Candidate{};
  g_target_by_service = Candidate{};
  g_best_candidate = Candidate{};
  g_scan_end_reason = 0;
  g_scan_end_seen = false;
  g_last_heartbeat_s = 0;
  g_start_retry_done = false;
}

void updateCandidate(Candidate& slot, const String& mac, const String& name, int rssi,
                     bool has_service) {
  if (!slot.valid || rssi > slot.rssi) {
    slot.valid = true;
    slot.mac = mac;
    slot.name = name;
    slot.rssi = rssi;
    slot.has_service = has_service;
  }
}

void onAdvertisedDevice(const NimBLEAdvertisedDevice* dev) {
  if (dev == nullptr) {
    return;
  }

  g_stats.advs++;
  const int rssi = dev->getRSSI();
  const String mac = String(dev->getAddress().toString().c_str());
  const bool has_name = dev->haveName();
  const String name = has_name ? String(dev->getName().c_str()) : "";
  const bool has_service = deviceHasHrService(dev);

  if (has_name) {
    g_stats.named++;
  }

  rememberUnique(mac);

  if (g_stats.best_mac.isEmpty() || rssi > g_stats.best_rssi) {
    g_stats.best_rssi = rssi;
    g_stats.best_mac = mac;
    g_stats.best_name = name;
  }

  if (name.indexOf("55825-1") >= 0) {
    updateCandidate(g_target_by_name, mac, name, rssi, has_service);
  }

  if (has_service) {
    updateCandidate(g_target_by_service, mac, name, rssi, has_service);
  }

  Serial.printf("[ALL][S2] adv mac=%s rssi=%d name=\"%s\"\n", mac.c_str(), rssi,
                name.c_str());
}

class ScanCallbacksNew : public NimBLEScanCallbacks {
 public:
  void onResult(const NimBLEAdvertisedDevice* dev) override { onAdvertisedDevice(dev); }

  void onScanEnd(const NimBLEScanResults&, int reason) override {
    g_scan_running = false;
    g_scan_end_seen = true;
    g_scan_end_reason = reason;
  }
};

class ScanCallbacksLegacy : public NimBLEAdvertisedDeviceCallbacks {
 public:
  void onResult(NimBLEAdvertisedDevice* dev) override { onAdvertisedDevice(dev); }
};

ScanCallbacksNew g_scan_callbacks_new;
ScanCallbacksLegacy g_scan_callbacks_legacy;

void setScanCallbacksCompat(NimBLEScan* scan) {
  if constexpr (HasSetScanCallbacks<NimBLEScan>::value) {
    scan->setScanCallbacks(&g_scan_callbacks_new, true);
    Serial.println("[ALL][S2] api=scan_callbacks");
  } else if constexpr (HasSetAdvertisedCallbacks<NimBLEScan>::value) {
    scan->setAdvertisedDeviceCallbacks(&g_scan_callbacks_legacy, true);
    Serial.println("[ALL][S2] api=advertised_device_callbacks");
  } else {
#error "Unsupported NimBLE-Arduino API: neither setScanCallbacks nor setAdvertisedDeviceCallbacks is available"
  }
}

void logBuildConfig(const char* name, bool enabled) {
  Serial.printf("[ALL] %s=%s\n", name, enabled ? "1" : "undefined");
}

void logS0() {
  Serial.printf("[ALL] build=%s %s\n", __DATE__, __TIME__);
  Serial.printf("[ALL] sdk=%s\n", ESP.getSdkVersion());
  Serial.printf("[ALL] chip_model=%s\n", ESP.getChipModel());
  Serial.printf("[ALL] chip_revision=%d\n", ESP.getChipRevision());
  Serial.printf("[ALL] heap=%u\n", ESP.getFreeHeap());
  Serial.printf("[ALL] psram=%u\n", ESP.getFreePsram());

#ifdef CONFIG_BT_ENABLED
  logBuildConfig("CONFIG_BT_ENABLED", true);
#else
  logBuildConfig("CONFIG_BT_ENABLED", false);
#endif
#ifdef CONFIG_BT_BLE_ENABLED
  logBuildConfig("CONFIG_BT_BLE_ENABLED", true);
#else
  logBuildConfig("CONFIG_BT_BLE_ENABLED", false);
#endif
#ifdef CONFIG_BT_NIMBLE_ENABLED
  logBuildConfig("CONFIG_BT_NIMBLE_ENABLED", true);
#else
  logBuildConfig("CONFIG_BT_NIMBLE_ENABLED", false);
#endif
#ifdef CONFIG_BT_CONTROLLER_ENABLED
  logBuildConfig("CONFIG_BT_CONTROLLER_ENABLED", true);
#else
  logBuildConfig("CONFIG_BT_CONTROLLER_ENABLED", false);
#endif
}

void runS1Bringup() {
#if ALL_HAS_NVS_FLASH_H
  esp_err_t rc = nvs_flash_init();
  if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    (void)nvs_flash_erase();
    rc = nvs_flash_init();
  }
  Serial.printf("[ALL][S1] nvs_flash_init rc=%d status=-\n", rc);
#else
  Serial.println("[ALL][S1] nvs_flash_init rc=undefined status=-");
#endif

#if ALL_HAS_ESP_BT_H
  esp_err_t rc_rel = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  Serial.printf("[ALL][S1] esp_bt_controller_mem_release(CLASSIC) rc=%d status=%d\n", rc_rel,
                getControllerStatus());

  const int st_before = getControllerStatus();
  Serial.printf("[ALL][S1] esp_bt_controller_get_status rc=0 status=%d\n", st_before);

  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  esp_err_t rc_init = esp_bt_controller_init(&bt_cfg);
  Serial.printf("[ALL][S1] esp_bt_controller_init rc=%d status=%d\n", rc_init,
                getControllerStatus());

  esp_err_t rc_enable = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  Serial.printf("[ALL][S1] esp_bt_controller_enable(BLE) rc=%d status=%d\n", rc_enable,
                getControllerStatus());
#else
  Serial.println("[ALL][S1] esp_bt_controller_mem_release(CLASSIC) rc=undefined status=undefined");
  Serial.println("[ALL][S1] esp_bt_controller_get_status rc=undefined status=undefined");
  Serial.println("[ALL][S1] esp_bt_controller_init rc=undefined status=undefined");
  Serial.println("[ALL][S1] esp_bt_controller_enable(BLE) rc=undefined status=undefined");
#endif

#if ALL_HAS_ESP_BT_MAIN_H
  int blue_before = getBluedroidStatus();
  Serial.printf("[ALL][S1] esp_bluedroid_get_status rc=0 status=%d\n", blue_before);

  esp_err_t rc_blue_init = esp_bluedroid_init();
  Serial.printf("[ALL][S1] esp_bluedroid_init rc=%d status=%d\n", rc_blue_init,
                getBluedroidStatus());

  esp_err_t rc_blue_enable = esp_bluedroid_enable();
  Serial.printf("[ALL][S1] esp_bluedroid_enable rc=%d status=%d\n", rc_blue_enable,
                getBluedroidStatus());
#else
  Serial.println("[ALL][S1] esp_bluedroid_get_status rc=undefined status=undefined");
  Serial.println("[ALL][S1] esp_bluedroid_init rc=undefined status=undefined");
  Serial.println("[ALL][S1] esp_bluedroid_enable rc=undefined status=undefined");
#endif
}

bool startScanRound() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (scan == nullptr) {
    Serial.println("[ALL][FAIL] scan object is null");
    return false;
  }

  const RoundCfg& cfg = kRoundPlan[g_round_index % (sizeof(kRoundPlan) / sizeof(kRoundPlan[0]))];

  setScanCallbacksCompat(scan);
  scan->setInterval(cfg.interval);
  scan->setWindow(cfg.window);
  scan->setActiveScan(cfg.active);
  scan->setDuplicateFilter(cfg.dup);
  scan->setMaxResults(0);
  scan->clearResults();

  g_scan_running = true;
  g_scan_end_seen = false;

  Serial.printf(
      "[ALL][S2] round %lu start params: interval=%u window=%u active=%d dup=%d\n",
      static_cast<unsigned long>(g_round_counter + 1), static_cast<unsigned int>(cfg.interval),
      static_cast<unsigned int>(cfg.window), cfg.active ? 1 : 0, cfg.dup ? 1 : 0);

  const bool started = scan->start(kRoundDurationSeconds, false, true);
  Serial.printf("[ALL][S2] start()=%d\n", started ? 1 : 0);
  return started;
}

void hardFailStartFalse() {
  Serial.printf(
      "[ALL][FAIL] start() false after retry; btStarted=%d ctrl=%d blue=%d heap=%u\n",
      getBtStartedStatus(), getControllerStatus(), getBluedroidStatus(), ESP.getFreeHeap());
  transition(S_ERR_HARDFAIL);
}

void printRoundSummary() {
  const String best = g_stats.best_mac.isEmpty()
                          ? String("n/a")
                          : (g_stats.best_name.isEmpty() ? g_stats.best_mac
                                                         : (g_stats.best_mac + " " + g_stats.best_name));
  Serial.printf("[ALL][S2] summary advs=%lu unique=%lu named=%lu best=%s rssi=%d\n",
                static_cast<unsigned long>(g_stats.advs),
                static_cast<unsigned long>(g_stats.unique),
                static_cast<unsigned long>(g_stats.named), best.c_str(), g_stats.best_rssi);

  if (!g_scan_end_seen) {
    Serial.println("[ALL][S2] note: no onScanEnd callback observed");
  }
  if (g_stats.unique_full) {
    Serial.printf("[ALL][S2] note: unique MAC capacity reached (%u)\n",
                  static_cast<unsigned int>(kMaxUniqueMacs));
  }
}

void hrNotifyCb(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  if (data == nullptr || len < 2) {
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

  g_notify_received = true;
  Serial.printf("[ALL][HR] bpm=%u flags=0x%02X len=%u\n", bpm, flags,
                static_cast<unsigned int>(len));
}

void doConnectAndSubscribe() {
  if (!g_best_candidate.valid) {
    Serial.println("[ALL][S4] no candidate to connect");
    transition(S2_SCAN_ROUND);
    return;
  }

  if (g_client == nullptr) {
    g_client = NimBLEDevice::createClient();
    if (g_client != nullptr) {
      g_client->setConnectTimeout(5);
    }
  }

  if (g_client == nullptr) {
    Serial.println("[ALL][S4] createClient failed");
    transition(S2_SCAN_ROUND);
    return;
  }

  const uint32_t backoff[] = {1000, 2000, 4000};
  bool connected = false;
  NimBLEAddress addr(g_best_candidate.mac.c_str());

  for (size_t i = 0; i < 3; ++i) {
    Serial.printf("[ALL][S4] connect try=%u/%u mac=%s\n", static_cast<unsigned int>(i + 1), 3,
                  g_best_candidate.mac.c_str());
    connected = g_client->connect(addr);
    Serial.printf("[ALL][S4] connect rc=%d\n", connected ? 1 : 0);
    if (connected) {
      break;
    }
    delay(backoff[i]);
  }

  if (!connected) {
    Serial.println("[ALL][S4] connect failed after retries");
    transition(S2_SCAN_ROUND);
    return;
  }

  NimBLERemoteService* svc = g_client->getService(kHrServiceUuid16);
  if (svc == nullptr) {
    Serial.println("[ALL][S4] service 0x180D not found -> disconnect");
    g_client->disconnect();
    transition(S2_SCAN_ROUND);
    return;
  }

  g_hr_char = svc->getCharacteristic(kHrMeasurementCharUuid16);
  if (g_hr_char == nullptr || !g_hr_char->canNotify()) {
    Serial.println("[ALL][S4] char 0x2A37 not notifiable -> disconnect");
    g_client->disconnect();
    transition(S2_SCAN_ROUND);
    return;
  }

  const bool subscribed = g_hr_char->subscribe(true, hrNotifyCb, false);
  Serial.printf("[ALL][S4] subscribe rc=%d\n", subscribed ? 1 : 0);
  if (!subscribed) {
    g_client->disconnect();
    transition(S2_SCAN_ROUND);
    return;
  }

  g_notify_received = false;
  g_notify_started_ms = millis();
  transition(S5_WAIT_NOTIFY);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  g_state_started_ms = millis();
  g_state_entry = true;
}

void loop() {
  switch (g_state) {
    case S0_BOOT_INFO:
      g_state_entry = false;
      logS0();
      transition(S1_BT_BRINGUP);
      break;

    case S1_BT_BRINGUP:
      g_state_entry = false;
      runS1Bringup();
      NimBLEDevice::init("");
      NimBLEDevice::setPower(ESP_PWR_LVL_P9);
      transition(S2_SCAN_ROUND);
      break;

    case S2_SCAN_ROUND: {
      if (g_state_entry) {
        g_state_entry = false;
        resetScanStats();
        if (!startScanRound()) {
          if (!g_start_retry_done) {
            g_start_retry_done = true;
            NimBLEDevice::deinit(true);
            delay(200);
            NimBLEDevice::init("");
            NimBLEDevice::setPower(ESP_PWR_LVL_P9);
            Serial.println("[ALL][S2] retrying start after deinit/init");
            if (!startScanRound()) {
              hardFailStartFalse();
            }
          } else {
            hardFailStartFalse();
          }
        }
      }

      if (g_scan_running) {
        const uint32_t elapsed_s = (millis() - g_state_started_ms) / 1000;
        while (g_last_heartbeat_s < elapsed_s && g_last_heartbeat_s < kRoundDurationSeconds) {
          g_last_heartbeat_s++;
          Serial.printf("[ALL][S2] t=%lus\n", static_cast<unsigned long>(g_last_heartbeat_s));
        }
      }

      if (g_scan_running && (millis() - g_state_started_ms) > (kRoundDurationSeconds + 2) * 1000UL) {
        g_scan_running = false;
        NimBLEDevice::getScan()->stop();
        Serial.println("[ALL][S2] timeout waiting for scan end");
      }

      if (!g_scan_running && (g_scan_end_seen || g_stats.advs > 0 || g_last_heartbeat_s >= kRoundDurationSeconds)) {
        while (g_last_heartbeat_s < kRoundDurationSeconds) {
          g_last_heartbeat_s++;
          Serial.printf("[ALL][S2] t=%lus\n", static_cast<unsigned long>(g_last_heartbeat_s));
        }
        printRoundSummary();
        transition(S3_FILTER_TARGET);
      }
      break;
    }

    case S3_FILTER_TARGET: {
      g_state_entry = false;
      g_best_candidate = Candidate{};
      if (g_target_by_name.valid) {
        g_best_candidate = g_target_by_name;
      } else if (g_target_by_service.valid) {
        g_best_candidate = g_target_by_service;
      }

      const String best_info = g_best_candidate.valid
                                   ? (g_best_candidate.mac + " " + String(g_best_candidate.rssi) +
                                      " \"" + g_best_candidate.name + "\"")
                                   : String("n/a");
      Serial.printf("[ALL][S3] target_by_name=%d target_by_service=%d best_candidate=%s\n",
                    g_target_by_name.valid ? 1 : 0, g_target_by_service.valid ? 1 : 0,
                    best_info.c_str());

      if (!g_best_candidate.valid) {
        g_round_counter++;
        g_round_index = g_round_counter % (sizeof(kRoundPlan) / sizeof(kRoundPlan[0]));
        transition(S2_SCAN_ROUND);
      } else {
        transition(S4_CONNECT_HRM);
      }
      break;
    }

    case S4_CONNECT_HRM:
      g_state_entry = false;
      doConnectAndSubscribe();
      break;

    case S5_WAIT_NOTIFY:
      g_state_entry = false;
      if (g_notify_received) {
        Serial.println("[ALL][S5] notify received");
        if (g_client != nullptr && g_client->isConnected()) {
          g_client->disconnect();
        }
        g_round_counter++;
        g_round_index = g_round_counter % (sizeof(kRoundPlan) / sizeof(kRoundPlan[0]));
        transition(S2_SCAN_ROUND);
      } else if ((millis() - g_notify_started_ms) >= kNotifyWaitMs) {
        Serial.println("[ALL][S5] no notify within 10s -> disconnect");
        if (g_client != nullptr && g_client->isConnected()) {
          g_client->disconnect();
        }
        g_round_counter++;
        g_round_index = g_round_counter % (sizeof(kRoundPlan) / sizeof(kRoundPlan[0]));
        transition(S2_SCAN_ROUND);
      }
      break;

    case S_ERR_HARDFAIL:
      g_state_entry = false;
      Serial.println("[ALL][FAIL] hard fail state; waiting before reboot loop");
      delay(3000);
      g_round_counter = 0;
      g_round_index = 0;
      transition(S0_BOOT_INFO);
      break;
  }

  delay(kStateTickMs);
}

}  // namespace
