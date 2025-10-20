#include "esp_err.h"
#include "esp_event.h"
#include "esp_event_base.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_types.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_types_generic.h"
#include "freertos/event_groups.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "portmacro.h"
#include <stdint.h>
#include <string.h>

// ---- Wifi menuconfig ------------------------------------
#define WIFI_SSID CONFIG_ESP_WIFI_SSID
#define WIFI_PASS CONFIG_ESP_WIFI_PASSWORD

// Max number of connection retries
#define MAX_RETRY CONFIG_ESP_MAX_RETRY

#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif
// ---- Wifi menuconfig ------------------------------------

#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
// -----------------------------------------------------------------

#define WIFI_SUCCESS BIT0
#define WIFI_FAILURE BIT1

// Task tag
static const char *WIFI_TAG = "WIFI";

// Event status
static EventGroupHandle_t wifi_event_group;

// Current number of retries
static int s_retry_num;

/* Wifi Handlers */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    ESP_LOGI(WIFI_TAG, "Connecting to AP");
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < MAX_RETRY) {
      ESP_LOGI(WIFI_TAG, "Reconnecting to AP");
      esp_wifi_connect();
      s_retry_num++;
    } else {
      xEventGroupSetBits(wifi_event_group, WIFI_FAILURE);
    }
  }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data) {
  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(WIFI_TAG, "Station IP: " IPSTR, IP2STR(&event->ip_info.ip));

    s_retry_num = 0;

    xEventGroupSetBits(wifi_event_group, WIFI_SUCCESS);
  }
}

esp_err_t connect_wifi() {
  int status = WIFI_FAILURE;

  /* Initialize wifi */
  // Setup TCP/IP stack
  ESP_ERROR_CHECK(esp_netif_init());

  // Initialize default event loop
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // Create wifi station
  esp_netif_create_default_wifi_sta();

  // Setup station with default config
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  /* Event loops */
  // Save the output of an event
  wifi_event_group = xEventGroupCreate();

  esp_event_handler_instance_t wifi_event_instance;
  esp_event_handler_instance_t got_ip_event_instance;

  // Call handler for any wifi event
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
      &wifi_event_instance));

  // Call handler for ip obtained event
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL,
      &got_ip_event_instance));

  /* Wifi config */
  wifi_config_t wifi_config = {
      .sta = {.ssid = WIFI_SSID,
              .password = WIFI_PASS,
              .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
              .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
              .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
              .pmf_cfg = {
                  .capable = true,
                  .required = false,
              }}};

  // Set wifi controller to be a station
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

  // Apply config
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

  // Start driver
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(WIFI_TAG, "WIFI station initialization complete");

  /* Wait for connection */
  // Wait until success or failure status is recieved
  EventBits_t bits =
      xEventGroupWaitBits(wifi_event_group, WIFI_SUCCESS | WIFI_FAILURE,
                          pdFALSE, pdFALSE, portMAX_DELAY);

  if (bits & WIFI_SUCCESS) {
    ESP_LOGI(WIFI_TAG, "Connected to AP");
    status = WIFI_SUCCESS;
  } else if (bits & WIFI_FAILURE) {
    ESP_LOGE(WIFI_TAG, "Failed to connect to AP");
    status = WIFI_FAILURE;
  } else {
    ESP_LOGE(WIFI_TAG, "Unexpected event");
    status = WIFI_FAILURE;
  }

  /* Unregister event handlers */
  ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               got_ip_event_instance));
  ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_instance));
  vEventGroupDelete(wifi_event_group);

  return status;
}
