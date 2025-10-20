#include "esp_err.h"
#include "ftp.c"
#include "nvs_flash.h"
#include "wifi.c"
#include <esp_log.h>
#include <esp_spiffs.h>
#include <stdio.h>

static const char *FS_TAG = "Filesystem setup";

void app_main(void) {
  // ---- Wifi setup ---------------------------------------------------
  esp_err_t status = WIFI_FAILURE;

  // Initialize storage
  esp_err_t res = nvs_flash_init();
  if (res == ESP_ERR_NVS_NO_FREE_PAGES ||
      res == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    res = nvs_flash_init();
  }
  ESP_ERROR_CHECK(res);

  // Connect to AP
  status = connect_wifi();
  if (WIFI_SUCCESS != status) {
    ESP_LOGE(WIFI_TAG, "Failed to associate to AP, dying...");
    return;
  }
  // ---- Wifi setup ---------------------------------------------------
}
