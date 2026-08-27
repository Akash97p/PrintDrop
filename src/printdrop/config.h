#pragma once

// PrintDrop — build-wide configuration.

#ifndef PRINTDROP_VERSION
#define PRINTDROP_VERSION "0.0.0-dev"
#endif

#define PRINTDROP_NAME "PrintDrop"

// ---------------------------------------------------------------------------
// SD bus selection
// ---------------------------------------------------------------------------
// feat/sdio: SDIO 4-bit via the ESP32-S3 SDMMC host is the default (USE_SDIO).
// Undefine it to build the legacy 4-wire SPI driver (env printdrop_spi).
// The two drivers share the same high-level contract (one mutex, withdraw
// before writing, remount when the host writes) so the rest of the firmware
// is bus-agnostic.

// SD card wiring — SPI (legacy, 4 wires, overridden from platformio.ini).
#ifndef SD_CS_PIN
#define SD_CS_PIN   12
#endif
#ifndef SD_MISO_PIN
#define SD_MISO_PIN 39
#endif
#ifndef SD_MOSI_PIN
#define SD_MOSI_PIN 14
#endif
#ifndef SD_CLK_PIN
#define SD_CLK_PIN  40
#endif

// Verified clean to 25 MHz on this wiring; 20 MHz leaves margin.
#ifndef SD_SPI_FREQ
#define SD_SPI_FREQ 20000000
#endif

// SD card wiring — SDIO 4-bit (feat/sdio, 6 wires, overridden from
// platformio.ini). Reuses the four SPI pins plus two new data lines so an
// SPI-wired board migrates with two extra jumpers. SDIO requires a 3.3 V-native
// microSD breakout (no AMS1117/LC125) with 10 k pull-ups on CMD and D0-D3.
#ifndef SDMMC_CLK_PIN
#define SDMMC_CLK_PIN 40
#endif
#ifndef SDMMC_CMD_PIN
#define SDMMC_CMD_PIN 14
#endif
#ifndef SDMMC_D0_PIN
#define SDMMC_D0_PIN  39
#endif
#ifndef SDMMC_D1_PIN
#define SDMMC_D1_PIN  12
#endif
#ifndef SDMMC_D2_PIN
#define SDMMC_D2_PIN  13
#endif
#ifndef SDMMC_D3_PIN
#define SDMMC_D3_PIN  15
#endif

// SDMMC bus width: 1 or 4. 4-bit is ~3-4x faster; 1-bit is useful for
// bring-up when only CLK/CMD/D0 are wired.
#ifndef SDMMC_WIDTH
#define SDMMC_WIDTH 4
#endif

// SDMMC frequency in Hz. 40 MHz is the SDHC high-speed limit; 20 MHz is
// conservative on jumper wiring and still ~4x faster than SPI at 20 MHz.
#ifndef SDMMC_FREQ
#define SDMMC_FREQ 40000000
#endif

// Fallback access point used when no credentials are stored, or the stored
// network cannot be joined.
#define AP_SSID_PREFIX  "PrintDrop-Setup"
#define AP_PASSWORD     "printdrop"      // WPA2 needs at least 8 characters
#define DEFAULT_HOSTNAME "printdrop"

// How long to wait for the configured network before falling back to the AP.
#define WIFI_CONNECT_TIMEOUT_MS 20000

// ---------------------------------------------------------------------------
// Status LED and factory-reset button (feat/ux)
// ---------------------------------------------------------------------------
// LED: heartbeat when idle (2 s period), rapid blink on SD/USB activity.
// Set to -1 to disable. Wiring: LED + resistor to GND (active-high) or 3V3
// (active-low) — define accordingly.
#ifndef PRINTDROP_LED_PIN
#define PRINTDROP_LED_PIN 38
#endif
#ifndef PRINTDROP_LED_ACTIVE_HIGH
#define PRINTDROP_LED_ACTIVE_HIGH 1
#endif

// Button: short press = eject/refresh printer view, long press 5-10 s = factory
// reset (clears Wi-Fi + auth). Active-low with internal pull-up by default.
#ifndef PRINTDROP_BUTTON_PIN
#define PRINTDROP_BUTTON_PIN 4
#endif
#ifndef PRINTDROP_BUTTON_ACTIVE_LOW
#define PRINTDROP_BUTTON_ACTIVE_LOW 1
#endif
#define BUTTON_FACTORY_RESET_MS 5000
#define BUTTON_EJECT_MS 50

// ---------------------------------------------------------------------------
// Web UI authentication (feat/ux)
// ---------------------------------------------------------------------------
// User and SHA-256 hex of password (64 chars). Stored in NVS namespace
// "printdrop" keys "auth_user"/"auth_hash"; build_flags supply the initial
// seed so a fresh flash is not open. Example: echo -n "newPass" | sha256sum
#ifndef PRINTDROP_AUTH_USER
#define PRINTDROP_AUTH_USER "admin"
#endif
#ifndef PRINTDROP_AUTH_PASS_HASH
#define PRINTDROP_AUTH_PASS_HASH "63402a9a36ef9d75badb66d958ad1decec6c9af4b8757ae77d3189ab0d6f3d68"
#endif
// Set to 0 to ship open (not recommended on shared LAN).
#ifndef PRINTDROP_AUTH_REQUIRED
#define PRINTDROP_AUTH_REQUIRED 1
#endif

// ---------------------------------------------------------------------------
// Discovery (feat/ux)
// ---------------------------------------------------------------------------
// mDNS is always on. LLMNR answers single-label names (e.g. http://printdrop)
// on Windows without Bonjour. Both advertise the same hostname.
#ifndef PRINTDROP_ENABLE_LLMNR
#define PRINTDROP_ENABLE_LLMNR 1
#endif
#ifndef PRINTDROP_ENABLE_MDNS
#define PRINTDROP_ENABLE_MDNS 1
#endif

// ---------------------------------------------------------------------------
// OTA (feat/ux)
// ---------------------------------------------------------------------------
// OTA via HTTP POST /api/ota and via SD card file `firmware.bin` + `firmware.json`.
// Requires partitions_printdrop_ota.csv (dual app slots). Define 0 to remove OTA
// code on very tight builds.
#ifndef PRINTDROP_ENABLE_OTA
#define PRINTDROP_ENABLE_OTA 1
#endif
#define OTA_SD_FIRMWARE_PATH "/firmware.bin"
#define OTA_SD_META_PATH     "/firmware.json"
