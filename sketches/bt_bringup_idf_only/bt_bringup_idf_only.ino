#include <Arduino.h>
#include <esp_err.h>
#include <nvs_flash.h>
#include <esp_bt.h>
#include <esp_bt_main.h>

extern "C" bool btStarted(void) __attribute__((weak));

static int btStartedSafe() {
  if (btStarted == nullptr) {
    return -1;
  }
  return btStarted() ? 1 : 0;
}

static void logErr(const char *tag, esp_err_t err) {
  Serial.printf("%s: %d (0x%X) %s\n",
                tag,
                static_cast<int>(err),
                static_cast<unsigned int>(err),
                esp_err_to_name(err));
}

static void logStatus(const char *prefix) {
  Serial.printf("%s btStarted=%d controller_status=%d bluedroid_status=%d\n",
                prefix,
                btStartedSafe(),
                static_cast<int>(esp_bt_controller_get_status()),
                static_cast<int>(esp_bluedroid_get_status()));
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.printf("[BTIDF] start sdk=%s chip=%s rev=%d heap=%u\n",
                ESP.getSdkVersion(),
                ESP.getChipModel(),
                static_cast<int>(ESP.getChipRevision()),
                static_cast<unsigned int>(ESP.getFreeHeap()));

#ifdef CONFIG_BT_ENABLED
  Serial.printf("[BTIDF] CONFIG_BT_ENABLED=%d\n", CONFIG_BT_ENABLED);
#else
  Serial.println("[BTIDF] CONFIG_BT_ENABLED=undefined");
#endif

#ifdef CONFIG_BT_CONTROLLER_ENABLED
  Serial.printf("[BTIDF] CONFIG_BT_CONTROLLER_ENABLED=%d\n", CONFIG_BT_CONTROLLER_ENABLED);
#else
  Serial.println("[BTIDF] CONFIG_BT_CONTROLLER_ENABLED=undefined");
#endif

#ifdef CONFIG_BT_BLE_ENABLED
  Serial.printf("[BTIDF] CONFIG_BT_BLE_ENABLED=%d\n", CONFIG_BT_BLE_ENABLED);
#else
  Serial.println("[BTIDF] CONFIG_BT_BLE_ENABLED=undefined");
#endif

#ifdef CONFIG_BT_NIMBLE_ENABLED
  Serial.printf("[BTIDF] CONFIG_BT_NIMBLE_ENABLED=%d\n", CONFIG_BT_NIMBLE_ENABLED);
#else
  Serial.println("[BTIDF] CONFIG_BT_NIMBLE_ENABLED=undefined");
#endif

#ifdef CONFIG_BT_BLUEDROID_ENABLED
  Serial.printf("[BTIDF] CONFIG_BT_BLUEDROID_ENABLED=%d\n", CONFIG_BT_BLUEDROID_ENABLED);
#else
  Serial.println("[BTIDF] CONFIG_BT_BLUEDROID_ENABLED=undefined");
#endif

  esp_err_t nvsRc = nvs_flash_init();
  logErr("[BTIDF] nvs_flash_init", nvsRc);
  if (nvsRc == ESP_ERR_NVS_NO_FREE_PAGES || nvsRc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    logErr("[BTIDF] nvs_flash_erase", nvs_flash_erase());
    nvsRc = nvs_flash_init();
    logErr("[BTIDF] nvs_flash_init(retry)", nvsRc);
  }

  logStatus("[BTIDF] status(before)");

  logErr("[BTIDF] mem_release(CLASSIC)", esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

  esp_bt_controller_config_t btCfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  esp_err_t initRc = esp_bt_controller_init(&btCfg);
  logErr("[BTIDF] controller_init", initRc);
  esp_err_t enableRc = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  logErr("[BTIDF] controller_enable(BLE)", enableRc);

  if (initRc == ESP_ERR_INVALID_STATE || enableRc == ESP_ERR_INVALID_STATE) {
    Serial.println("[BTIDF] INVALID_STATE detected -> disable+deinit+retry");
    logErr("[BTIDF] controller_disable(pre)", esp_bt_controller_disable());
    logErr("[BTIDF] controller_deinit(pre)", esp_bt_controller_deinit());

    initRc = esp_bt_controller_init(&btCfg);
    logErr("[BTIDF] controller_init(retry)", initRc);
    enableRc = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    logErr("[BTIDF] controller_enable(BLE)(retry)", enableRc);
  }

  logStatus("[BTIDF] status(after)");

  bool enabled = (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED);
  Serial.printf("[BTIDF] RESULT: controller_enabled=%d last_init=0x%X last_enable=0x%X\n",
                enabled ? 1 : 0,
                static_cast<unsigned int>(initRc),
                static_cast<unsigned int>(enableRc));
  if (enableRc == ESP_ERR_INVALID_STATE || initRc == ESP_ERR_INVALID_STATE) {
    Serial.println("[BTIDF] RESULT: ESP_ERR_INVALID_STATE (0x103) observed without NimBLE include");
  } else {
    Serial.println("[BTIDF] RESULT: no ESP_ERR_INVALID_STATE (0x103) observed in IDF-only bringup");
  }

  Serial.println("[BTIDF] HALT");
  while (true) {
    delay(1000);
  }
}

void loop() {
}
