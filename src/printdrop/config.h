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
