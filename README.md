<div align="center">

# USB Stick Emulator — ESP32-S3

**Turn an ESP32-S3 and an SD card into a USB mass storage device.**

[![Platform](https://img.shields.io/badge/platform-ESP32--S3-E7352C?logo=espressif&logoColor=white)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Framework](https://img.shields.io/badge/framework-Arduino-00979D?logo=arduino&logoColor=white)](https://github.com/espressif/arduino-esp32)
[![Build](https://img.shields.io/badge/build-PlatformIO-FF7F00?logo=platformio&logoColor=white)](https://platformio.org/)
[![USB](https://img.shields.io/badge/USB-TinyUSB%20MSC-336791)](https://github.com/hathach/tinyusb)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Status](https://img.shields.io/badge/status-working-brightgreen)](ox-alpha/FINDINGS.md)

</div>

---

Exposes an SPI-attached SD card to a USB host as a standard mass storage
device, so the board behaves like an ordinary USB stick. Written for a bare
**ESP32-S3-DevKitC-1** — no display, no keyboard, no vendor board support
package.

Verified end to end: the card mounts on Windows as a removable FAT32 volume,
and a 2 MB write survives a SHA-256 round trip.

| Measurement | Value |
|---|---|
| Read throughput (uncached) | 485 KB/s |
| Write throughput | 248 KB/s |
| Verified SPI clock | up to 25 MHz (runs at 20 MHz) |
| Card tested | 32 GB SDHC, FAT32 |

Throughput is bounded by driving the card in SPI mode rather than 4-bit SDIO.

## Hardware

- ESP32-S3-DevKitC-1 (this tree is configured for a **4 MB flash, no PSRAM** board)
- SD/microSD breakout wired for SPI, powered from **3V3**
- A **FAT32** card — exFAT will not mount
- Both USB ports connected: the native port becomes the disk, the UART bridge
  carries the console

### Wiring

| Signal | GPIO |
|--------|------|
| CS     | 12   |
| MISO   | 39   |
| MOSI   | 14   |
| CLK    | 40   |

Pins are set through `build_flags` in `platformio.ini`, with matching defaults
in each source file.

> **Power the module from 3V3.** Modules with an AMS1117 regulator also run on
> 5 V, but their pull-ups then reference the 5 V rail and drive the ESP32's
> GPIOs above 3.3 V, which is outside the SoC's absolute maximum.

## Quick start

```bash
pio run -e msc -t upload     # flash the firmware
```

Reset the board; the card appears as a removable drive on the host.

## Build environments

| Env | Purpose | Console |
|---|---|---|
| `msc` | The firmware — SD card as a USB drive | UART0 (`COM5`) |
| `ramdisk` | RAM-backed FAT12 volume; proves USB MSC works without the SD card | UART0 (`COM5`) |
| `diag` | SPI speed sweep, card geometry, MBR dump, root listing | USB/JTAG (`COM11`) |
| `scan` | Pin health, line voltages, pin-permutation sweep; repeats every 10 s | USB/JTAG (`COM11`) |

Troubleshooting order: `scan` when nothing is detected, `diag` when it mounts
but misbehaves, `ramdisk` to prove the USB path independently of the card.

## Reflashing

`msc` and `ramdisk` hand the native USB port to TinyUSB, so the USB-Serial-JTAG
port disappears and `esptool` cannot reach the board there. Every firmware
therefore watches UART0 for the word `BOOTLOADER` and reboots into download
mode:

```powershell
"BOOTLOADER" | Out-File -Encoding ascii COM5
```

Then flash normally. Manual fallback: hold **BOOT**, tap **RESET**, release
**BOOT**.

## Gotchas

Three details stop the disk from appearing at all, and none of them produce a
useful error message:

- **`ARDUINO_USB_MODE` must be `0`.** The stock board definition hard-codes `1`,
  which compiles `USBMSC` out entirely. Override it through
  `board_build.extra_flags` — appending to `build_flags` only yields a macro
  redefinition.
- **`ARDUINO_USB_CDC_ON_BOOT` must be `0`.** It implies `ARDUINO_USB_ON_BOOT`,
  which makes the core call `USB.begin()` in `app_main()` *before* `setup()`
  runs — freezing the USB descriptor before the sketch can add its MSC
  interface or set its own PID.
- **SD cards must be identified at ≤400 kHz** before the clock is raised.
  Calling `SD.begin()` directly at 20 MHz fails on a cold card.

The full investigation — including the wiring faults found along the way — is
in [`ox-alpha/FINDINGS.md`](ox-alpha/FINDINGS.md).

## Roadmap

- [ ] Wi-Fi file access with a web UI served from LittleFS
- [ ] Drag-and-drop upload, grid and list views
- [ ] mDNS / static IP for a stable hostname

## License

MIT — see [LICENSE](LICENSE). Derived from the original
`Esp32-USB-Stick` project for the M5Stack Cardputer.
