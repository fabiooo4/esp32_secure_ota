#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"

#define WIFI_SUCCESS BIT0
#define WIFI_FAILURE BIT1

static const char *WIFI_TAG = "WIFI";

// Connects to wifi access point with defined SSID and password in menuconfig
// Returns WIFI_SUCCESS or WIFI_FAILURE
esp_err_t connect_wifi();

#endif
