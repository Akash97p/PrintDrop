#pragma once

// PrintDrop — build-wide configuration.

#ifndef PRINTDROP_VERSION
#define PRINTDROP_VERSION "0.0.0-dev"
#endif

#define PRINTDROP_NAME "PrintDrop"

// SD card wiring (overridden from platformio.ini build_flags).
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

// Fallback access point used when no credentials are stored, or the stored
// network cannot be joined.
#define AP_SSID_PREFIX  "PrintDrop-Setup"
#define AP_PASSWORD     "printdrop"      // WPA2 needs at least 8 characters
#define DEFAULT_HOSTNAME "printdrop"

// How long to wait for the configured network before falling back to the AP.
#define WIFI_CONNECT_TIMEOUT_MS 20000
