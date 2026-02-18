#include <Arduino.h>
#include "esp32-hal-bt.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"

static void logErr(const char *tag, esp_err_t err) {
  Serial.printf("%s: %d (0x%X) %s\n", tag, static_cast<int>(err), static_cast<unsigned int>(err), esp_err_to_name(err));
}

static void logStatus(const char *prefix) {
  Serial.printf("%s btStarted=%d controller_status=%d bluedroid_status=%d\n",
                prefix,
                btStarted() ? 1 : 0,
                static_cast<int>(esp_bt_controller_get_status()),
                static_cast<int>(esp_bluedroid_get_status()));
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.printf("[BT1] start sdk=%s chip=%s rev=%d heap=%u\n",
                ESP.getSdkVersion(),
                ESP.getChipModel(),
                static_cast<int>(ESP.getChipRevision()),
                static_cast<unsigned int>(ESP.getFreeHeap()));

#ifdef CONFIG_BT_ENABLED
  Serial.printf("[BT1] CONFIG_BT_ENABLED=%d\n", CONFIG_BT_ENABLED);
#else
  Serial.println("[BT1] CONFIG_BT_ENABLED=undefined");
#endif

#ifdef CONFIG_BT_CONTROLLER_ENABLED
  Serial.printf("[BT1] CONFIG_BT_CONTROLLER_ENABLED=%d\n", CONFIG_BT_CONTROLLER_ENABLED);
#else
  Serial.println("[BT1] CONFIG_BT_CONTROLLER_ENABLED=undefined");
#endif

#ifdef CONFIG_BT_BLE_ENABLED
  Serial.printf("[BT1] CONFIG_BT_BLE_ENABLED=%d\n", CONFIG_BT_BLE_ENABLED);
#else
  Serial.println("[BT1] CONFIG_BT_BLE_ENABLED=undefined");
#endif

#ifdef CONFIG_BT_NIMBLE_ENABLED
  Serial.printf("[BT1] CONFIG_BT_NIMBLE_ENABLED=%d\n", CONFIG_BT_NIMBLE_ENABLED);
#else
  Serial.println("[BT1] CONFIG_BT_NIMBLE_ENABLED=undefined");
#endif

#ifdef CONFIG_BT_BLUEDROID_ENABLED
  Serial.printf("[BT1] CONFIG_BT_BLUEDROID_ENABLED=%d\n", CONFIG_BT_BLUEDROID_ENABLED);
#else
  Serial.println("[BT1] CONFIG_BT_BLUEDROID_ENABLED=undefined");
#endif

  esp_err_t e = nvs_flash_init();
  logErr("[BT1] nvs_flash_init", e);
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    esp_err_t eraseErr = nvs_flash_erase();
    logErr("[BT1] nvs_flash_erase", eraseErr);
    e = nvs_flash_init();
    logErr("[BT1] nvs_flash_init(retry)", e);
  }

  logStatus("[BT1] status(before)");

  logErr("[BT1] mem_release(CLASSIC)", esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  logErr("[BT1] controller_init", esp_bt_controller_init(&bt_cfg));
  logErr("[BT1] controller_enable(BLE)", esp_bt_controller_enable(ESP_BT_MODE_BLE));

  logStatus("[BT1] status(after)");

  bool enabled = false;
#ifdef ESP_BT_CONTROLLER_STATUS_ENABLED
  enabled = (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED);
#else
  enabled = btStarted();
#endif

  if (!enabled) {
    Serial.println("[BT1] HARD FAIL: controller not enabled. Stop.");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("[BT1] SUCCESS: controller enabled.");
  while (true) {
    delay(1000);
  }
}

void loop() {
}
