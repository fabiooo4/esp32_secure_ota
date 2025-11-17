#include "freertos/FreeRTOS.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

#include "ota.h"
#include "wifi.h"

static const char *APP_TAG = "APP";

// Main application to run alongside ota update
static void application(void *pvParameter) {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_app_desc_t running_app_info;

  while (1) {
    if (esp_ota_get_partition_description(running, &running_app_info) ==
        ESP_OK) {
      ESP_LOGI(APP_TAG, "Hello, from firmware version: %s!",
               running_app_info.version);
    } else {
      ESP_LOGI(APP_TAG, "Hello, from unknown firmware version!");
    }
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}

void app_main(void) {
  // ---- Storage setup ------------------------------------------------
  esp_err_t res = nvs_flash_init();
  if (res == ESP_ERR_NVS_NO_FREE_PAGES ||
      res == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    res = nvs_flash_init();
  }
  ESP_ERROR_CHECK(res);
  // ---- Storage setup ------------------------------------------------

  diagnose_new_firmware();

  // ---- Wifi setup ---------------------------------------------------
  esp_err_t status = WIFI_FAILURE;

  // Connect to AP
  status = connect_wifi();
  if (WIFI_SUCCESS != status) {
    ESP_LOGE(WIFI_TAG, "Failed to associate to AP, dying...");
    return;
  }
  // ---- Wifi setup ---------------------------------------------------

  xTaskCreate(&download_new_firmware, "download_new_firmware", 8192, NULL, 5,
              NULL);
  xTaskCreate(&application, "application", 2048, NULL, 5, NULL);

  // Free main task
  vTaskDelete(NULL);
}
