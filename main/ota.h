#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

// Download and write new firmware to the OTA partition from the configured HTTP
// server
void download_new_firmware(void *pvParameter);

// Check if the new firmware works as expected on first boot.
// If yes, mark app as valid to avoid rollback.
// If not, rollback to previous version.
void diagnose_new_firmware();

#endif
