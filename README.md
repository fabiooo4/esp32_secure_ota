# Secure Over The Air updates

This ESP32 project is an implementation of Over The Air Updates with Secure Boot
and Flash Encryption

## Prerequisites

- ESP32 development board
- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32/get-started/index.html#ide) v5.4.1 or later

## Installation

1. Clone the repository:
   ```
   git clone https://github.com/fabiooo4/esp32_secure_ota.git
   cd esp32_secure_ota
   ```
2. Build the project:
   ```
   idf.py build
   ```
3. Flash the project to the ESP32 (flash also builds the project):
   ```
   idf.py flash
   ```
4. Monitor the ESP32:
   ```
   idf.py monitor
   ```

# Configuration

The project is configured using the ESP-IDF menuconfig tool. To open the
configuration menu, run the following command:

```
idf.py menuconfig
```

## Over The Air Updates configuration

All the possible Over The Air Updates configurations can be found in the `menuconfig`
under `Over The Air Updates configuration`.

### Wifi configuration

In `Wifi configuration` set the SSID and password of the wifi network
you want to connect to. You can also change other parameters.

### Upgrade Server

In `Upgrade Server` set the url of the upgrade server where the updated binary is stored.
The url must contain the full path to a valid binary file in the following format:
```
https://<host-ip-address>:<host-port>/<firmware-image-filename>
```
For example:
```
https://192.168.2.106:8070/binary.bin
```

**Note**: The server part of this URL (e.g. `192.168.2.106`) must match the **CN** field used
when [generating the certificate and key](#ssl-certificate-generation).

The server requires an SSL certificate, but this can be disabled.

# Testing server

In the `server/` directory there is a python script that runs a test server providing an
updated binary. The binary must be in the `server/` directory with the name matching
the one set in the `Upgrade Server` configuration url.

## SSL Certificate generation
To generate a self-signed SSL certificate and private key do the following:
- Enter the `server/` directory, e.g. `cd server/`
- Run the following command to generate a self-signed SSL certificate and private key:
`openssl req -x509 -newkey rsa:2048 -keyout ca_key.pem -out ca_cert.pem -days 365 -nodes`
  - When prompted for the `Common Name (CN)`, enter the IP address of the server that will host the binary file.
  When using the test server, this should match the host IP address in the `Upgrade Server` configuration URL.
  - This will generate two files: `ca_cert.pem` (the certificate) and `ca_key.pem` (the private key).
- The `server/` directory should contain the updated firmware, e.g. `binary.bin`. This can be any valid ESP-IDF
  application, as long as its filename corresponds to the name configured using the `Upgrade Server` configuration URL
  in menuconfig. The only difference to flashing a firmware via the serial interface is that the binary is flashed to
  the `factory` partition, while OTA update use one of the OTA partitions.

## Flash certificate to board
Copy the generated certificate to `server_certs/` directory so it can be flashed on the device along with
the firmware:
```
cp server/ca_cert.pem server_certs/
```

## Running the server
To run the server execute the following:

```
python pytest_simple_ota.py <path_to_server_dir> <port> [cert_dir]
```
For example, in the `server/` directory:

```
python pytest_simple_ota.py . 8070
```

## Internal workflow of the OTA Example

After booting, the firmware:

1. Connects via the AP using the provided SSID and password
2. Connects to the HTTPS server and downloads the new image
3. Writes the image to flash, and instructs the bootloader to boot from this image after the next reset
4. Reboots

If you want to rollback to the `factory` app after the upgrade (or to the first OTA partition in case the `factory` partition does not exist), run the command `idf.py erase_otadata`. This restores the `ota_data` partition to its initial state.

## Supporting Rollback

This feature allows you to roll back to a previous firmware if new image is not usable. The menuconfig option
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` allows you to track the first boot of the application.
The ``native_ota_example`` contains code to demonstrate how a rollback works.

## Support for Versioning of Applications

Versioning allows to check the version of the application and prevent infinite
firmware update loops. Only newer applications are downloaded. Version checking is performed after the first firmware 
image package containing version data is received. The application version is obtained from one of three places:

1. If the `CONFIG_APP_PROJECT_VER_FROM_CONFIG` option is set, the value of `CONFIG_APP_PROJECT_VER` is used
2. Else, if the ``PROJECT_VER`` variable is set in the project `CMakeLists.txt` file, this value is used
3. Else, if the file ``$PROJECT_PATH/version.txt`` exists, its contents are used as ``PROJECT_VER``
4. Else, if the project is located in a Git repository, the output of ``git describe`` is used
5. Otherwise, ``PROJECT_VER`` will be "1"

In this project, ``$PROJECT_PATH/version.txt`` is used to define the app version.
Change the version in the file to compile the new firmware.

# References

- [Over The Air Updates Docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/ota.html)
