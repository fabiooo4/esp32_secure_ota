#include "errno.h"
#include "esp_app_format.h"
#include "esp_flash_partitions.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include <inttypes.h>

#define BUFFSIZE 1024
#define HASH_LEN 32 /* SHA-256 digest length */

static const char *OTA_TAG = "OTA";

// OTA data write buffer to write to the flash
static char ota_write_data[BUFFSIZE + 1] = {0};
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

static void print_sha256(const uint8_t *image_hash, const char *label) {
  char hash_print[HASH_LEN * 2 + 1];
  hash_print[HASH_LEN * 2] = 0;
  for (int i = 0; i < HASH_LEN; ++i) {
    sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
  }
  ESP_LOGI(OTA_TAG, "%s: %s", label, hash_print);
}

static void __attribute__((noreturn)) task_fatal_error(void) {
  ESP_LOGE(OTA_TAG, "Exiting task due to fatal error...");
  (void)vTaskDelete(NULL);

  while (1) {
    ;
  }
}

static void http_cleanup(esp_http_client_handle_t client) {
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
}

// Check if the new app image is valid and works as expected
// For testing, the app is always considered valid
static bool diagnostic(void) {
  bool diagnostic_is_ok = true;
  ESP_LOGI(OTA_TAG, "Running diagnostics ...");
  vTaskDelay(5000 / portTICK_PERIOD_MS);
  return diagnostic_is_ok;
}

// Infinite loop until a new firmware is available (simulated here by
// pressing the reset button)
static void wait_for_new_fiwmware(void) {
  ESP_LOGI(OTA_TAG, "When a new firmware is available on the server, press the "
                "reset button to download it");
  while (1) {
    // ESP_LOGI(OTA_TAG, "Waiting for a new firmware ...");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

// Download and write new firmware to the OTA partition from the configured HTTP
// server
void download_new_firmware(void *pvParameter) {
  esp_err_t err;
  // Handle set by esp_ota_begin(), must be freed via esp_ota_end()
  esp_ota_handle_t update_handle = 0;
  const esp_partition_t *update_partition = NULL;

  ESP_LOGI(OTA_TAG, "Starting new firmware download task");

  // ---- Check current partition --------------------------------------
  const esp_partition_t *configured = esp_ota_get_boot_partition();
  const esp_partition_t *running = esp_ota_get_running_partition();

  if (configured != running) {
    ESP_LOGW(OTA_TAG,
             "Configured OTA boot partition at offset 0x%08" PRIx32
             ", but running from offset 0x%08" PRIx32,
             configured->address, running->address);
    ESP_LOGW(OTA_TAG, "(This can happen if either the OTA boot data or preferred "
                  "boot image become corrupted somehow.)");
  }
  ESP_LOGI(OTA_TAG, "Running partition type %d subtype %d (offset 0x%08" PRIx32 ")",
           running->type, running->subtype, running->address);
  // ---- Check current partition --------------------------------------

  // ---- Connect to HTTP server ---------------------------------------
  esp_http_client_config_t config = {
      .url = CONFIG_FIRMWARE_UPG_URL,
      .cert_pem = (char *)server_cert_pem_start,
      .timeout_ms = CONFIG_OTA_RECV_TIMEOUT,
      .keep_alive_enable = true,
  };

#ifdef CONFIG_SKIP_COMMON_NAME_CHECK
  config.skip_cert_common_name_check = true;
#endif

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL) {
    ESP_LOGE(OTA_TAG, "Failed to initialise HTTP connection with the firmware upgrade server");
    task_fatal_error();
  }
  err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(OTA_TAG, "Failed to open HTTP connection with the firmware upgrade server: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    task_fatal_error();
  }
  esp_http_client_fetch_headers(client);
  // ---- Connect to HTTP server ---------------------------------------

  update_partition = esp_ota_get_next_update_partition(NULL);
  assert(update_partition != NULL);
  ESP_LOGI(OTA_TAG, "Writing to partition subtype %d at offset 0x%" PRIx32,
           update_partition->subtype, update_partition->address);

  // Handle recieved packet
  int binary_file_length = 0;
  bool image_header_was_checked = false;
  while (1) {
    int data_read = esp_http_client_read(client, ota_write_data, BUFFSIZE);
    if (data_read < 0) {
      ESP_LOGE(OTA_TAG, "Error: SSL data read error");
      http_cleanup(client);
      task_fatal_error();
    } else if (data_read > 0) {
      if (image_header_was_checked == false) {
        esp_app_desc_t new_app_info;

        if (data_read <= sizeof(esp_image_header_t) +
                             sizeof(esp_image_segment_header_t) +
                             sizeof(esp_app_desc_t)) {
          ESP_LOGE(OTA_TAG, "received package is not fit len");
          http_cleanup(client);
          esp_ota_abort(update_handle);
          task_fatal_error();
        }

        if (data_read > sizeof(esp_image_header_t) +
                            sizeof(esp_image_segment_header_t) +
                            sizeof(esp_app_desc_t)) {

          // ---- Version check ------------------------------------------------
          // Check current version with the new one
          memcpy(&new_app_info,
                 &ota_write_data[sizeof(esp_image_header_t) +
                                 sizeof(esp_image_segment_header_t)],
                 sizeof(esp_app_desc_t));
          ESP_LOGI(OTA_TAG, "New firmware version: %s", new_app_info.version);

          esp_app_desc_t running_app_info;
          if (esp_ota_get_partition_description(running, &running_app_info) ==
              ESP_OK) {
            ESP_LOGI(OTA_TAG, "Running firmware version: %s",
                     running_app_info.version);
          }

          const esp_partition_t *last_invalid_app =
              esp_ota_get_last_invalid_partition();
          esp_app_desc_t invalid_app_info;
          if (esp_ota_get_partition_description(last_invalid_app,
                                                &invalid_app_info) == ESP_OK) {
            ESP_LOGI(OTA_TAG, "Last invalid firmware version: %s",
                     invalid_app_info.version);
          }

          // Check current version with last invalid partition
          if (last_invalid_app != NULL) {
            if (memcmp(invalid_app_info.version, new_app_info.version,
                       sizeof(new_app_info.version)) == 0) {
              ESP_LOGW(OTA_TAG, "New version is the same as invalid version.");
              ESP_LOGW(OTA_TAG,
                       "Previously, there was an attempt to launch the "
                       "firmware with %s version, but it failed.",
                       invalid_app_info.version);
              ESP_LOGW(
                  OTA_TAG,
                  "The firmware has been rolled back to the previous version.");
              http_cleanup(client);
              wait_for_new_fiwmware();
            }
          }

#ifndef CONFIG_EXAMPLE_SKIP_VERSION_CHECK
          if (memcmp(new_app_info.version, running_app_info.version,
                     sizeof(new_app_info.version)) == 0) {
            ESP_LOGW(OTA_TAG, "Current running version is the same as a new. We "
                          "will not continue the update.");
            http_cleanup(client);
            wait_for_new_fiwmware();
          }
#endif
          // ---- Version check ------------------------------------------------

          image_header_was_checked = true;

          // ---- Begin OTA segment write to partition -------------------------
          err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES,
                              &update_handle);
          if (err != ESP_OK) {
            ESP_LOGE(OTA_TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
            http_cleanup(client);
            esp_ota_abort(update_handle);
            task_fatal_error();
          }
          ESP_LOGI(OTA_TAG, "esp_ota_begin succeeded");
          ESP_LOGI(OTA_TAG, "Updating firmware...");
          // ---- Begin OTA segment write to partition -------------------------
        }
      }

      // --- Write new firmware segment to partition --------------------------
      err =
          esp_ota_write(update_handle, (const void *)ota_write_data, data_read);
      if (err != ESP_OK) {
        http_cleanup(client);
        esp_ota_abort(update_handle);
        task_fatal_error();
      }

      binary_file_length += data_read;
      ESP_LOGD(OTA_TAG, "Written image length %d", binary_file_length);
      // --- Write new firmware segment to partition --------------------------
    } else if (data_read == 0) {
      /*
       * As esp_http_client_read never returns negative error code, we rely on
       * `errno` to check for underlying transport connectivity closure if any
       */
      if (errno == ECONNRESET || errno == ENOTCONN) {
        ESP_LOGE(OTA_TAG, "Connection closed, errno = %d", errno);
        break;
      }
      if (esp_http_client_is_complete_data_received(client) == true) {
        ESP_LOGI(OTA_TAG, "Connection closed");
        break;
      }
    }
  }

  // --- Check written firmware -------------------------------------------
  ESP_LOGI(OTA_TAG, "Total Write binary data length: %d", binary_file_length);

  if (esp_http_client_is_complete_data_received(client) != true) {
    ESP_LOGE(OTA_TAG, "Error in receiving complete file");
    http_cleanup(client);
    esp_ota_abort(update_handle);
    task_fatal_error();
  }

  err = esp_ota_end(update_handle);
  if (err != ESP_OK) {
    if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
      ESP_LOGE(OTA_TAG, "Image validation failed, image is corrupted");
    } else {
      ESP_LOGE(OTA_TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
    }
    http_cleanup(client);
    task_fatal_error();
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    ESP_LOGE(OTA_TAG, "esp_ota_set_boot_partition failed (%s)!",
             esp_err_to_name(err));
    http_cleanup(client);
    task_fatal_error();
  }
  // --- Check written firmware -------------------------------------------

  // Apply the update
  ESP_LOGI(OTA_TAG, "Prepare to restart system!");
  esp_restart();
  return;
}
// Check if the new firmware works as expected on first boot.
// If yes, mark app as valid to avoid rollback.
// If not, rollback to previous version.
void diagnose_new_firmware(void) {
  uint8_t sha_256[HASH_LEN] = {0};
  esp_partition_t partition;

  // Get sha256 digest for the partition table
  partition.address = ESP_PARTITION_TABLE_OFFSET;
  partition.size = ESP_PARTITION_TABLE_MAX_LEN;
  partition.type = ESP_PARTITION_TYPE_DATA;
  esp_partition_get_sha256(&partition, sha_256);
  print_sha256(sha_256, "SHA-256 for the partition table: ");

  // Get sha256 digest for bootloader
  partition.address = ESP_BOOTLOADER_OFFSET;
  partition.size = ESP_PARTITION_TABLE_OFFSET;
  partition.type = ESP_PARTITION_TYPE_APP;
  esp_partition_get_sha256(&partition, sha_256);
  print_sha256(sha_256, "SHA-256 for bootloader: ");

  // Get sha256 digest for running partition
  esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
  print_sha256(sha_256, "SHA-256 for current firmware: ");

  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;

  // Check validity of new app image on first boot
  if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
      bool diagnostic_is_ok = diagnostic();
      if (diagnostic_is_ok) {
        ESP_LOGI(
            OTA_TAG,
            "Diagnostics completed successfully! Continuing execution ...");
        esp_ota_mark_app_valid_cancel_rollback();
      } else {
        ESP_LOGE(
            OTA_TAG,
            "Diagnostics failed! Start rollback to the previous version ...");
        esp_ota_mark_app_invalid_rollback_and_reboot();
      }
    }
  }
}
