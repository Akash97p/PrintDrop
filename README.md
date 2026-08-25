<div align="center">

# PrintDrop

**A Wi-Fi SD card for 3D printers.**
Drop a print job from your desk instead of walking a USB stick to the machine.

[![Platform](https://img.shields.io/badge/platform-ESP32--S3-E7352C?logo=espressif&logoColor=white)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Framework](https://img.shields.io/badge/framework-Arduino-00979D?logo=arduino&logoColor=white)](https://github.com/espressif/arduino-esp32)
[![Build](https://img.shields.io/badge/build-PlatformIO-FF7F00?logo=platformio&logoColor=white)](https://platformio.org/)
[![USB](https://img.shields.io/badge/USB-TinyUSB%20MSC-336791)](https://github.com/hathach/tinyusb)
[![UI](https://img.shields.io/badge/UI-LittleFS%20hosted-4B32C3)](data/)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Status](https://img.shields.io/badge/status-working%20on%20hardware-brightgreen)](ox-alpha/FINDINGS.md)
[![Issues](https://img.shields.io/github/issues/Akash97p/PrintDrop)](https://github.com/Akash97p/PrintDrop/issues)
[![Last commit](https://img.shields.io/github/last-commit/Akash97p/PrintDrop)](https://github.com/Akash97p/PrintDrop/commits)

</div>

---

PrintDrop plugs into a 3D printer's USB port and appears as an ordinary USB
flash drive — no printer firmware changes, no cloud service, no vendor account.
At the same time it joins the local network and serves a web UI, so anyone can
drag a `.gcode` file onto the card from their desk and then walk over and press
print.

Built for printers that only accept USB media and have no networking of their
own.

Working on hardware: the card enumerates as a USB drive, the web UI serves from
LittleFS, and files uploaded over Wi-Fi appear to the USB host without a
reboot.

| Measurement | Value |
|---|---|
| Read throughput (USB, uncached) | 485 KB/s |
| Write throughput (USB) | 248 KB/s |
| SD clock | 20 MHz (verified clean to 25 MHz) |
| Web UI size | 62 KB, served from LittleFS |
| Card tested | 32 GB SDHC, FAT32 |

## How the USB and Wi-Fi sides coexist

This is the part that matters, and the part that is easy to get wrong.

A USB host caches the FAT. If the ESP32 writes to the card while the printer
has it mounted, the host's cached allocation table goes stale and the next
write from either side corrupts the filesystem. The reverse is also true — the
ESP32's own FATFS cache goes stale if the host writes sectors underneath it.

`src/printdrop/storage.cpp` enforces three rules:

1. **One mutex.** Every SD access, from either side, is serialised.
2. **Withdraw before writing.** Before the ESP32 modifies the card, the media is
   withdrawn from the USB host, then re-presented afterwards. The printer sees a
   card removal and re-reads its file list, so uploads appear without a reboot.
3. **Remount when the host writes.** MSC write callbacks set a flag; the ESP32
   remounts FATFS before trusting its own view of the filesystem again.

MSC callbacks take the mutex with a short timeout and fail the transfer rather
than stall the USB task, so a slow Wi-Fi upload can never hang the printer.

## Hardware

- ESP32-S3-DevKitC-1 (configured for a **4 MB flash, no PSRAM** board)
- SD/microSD breakout wired for SPI, powered from **3V3**
- A **FAT32** card — exFAT will not mount
- Printer on the native USB port; the UART bridge carries the console

### Wiring

| Signal | GPIO |
|--------|------|
| CS     | 12   |
| MISO   | 39   |
| MOSI   | 14   |
| CLK    | 40   |

> **Power the module from 3V3.** Modules with an AMS1117 regulator also run on
> 5 V, but their pull-ups then reference the 5 V rail and drive the ESP32's
> GPIOs above 3.3 V, outside the SoC's absolute maximum.

## Flashing

```bash
pio run -e printdrop -t upload      # firmware
pio run -e printdrop -t uploadfs    # web UI into LittleFS
```

Both are needed on a first install. See [Reflashing](#reflashing) for how to get
the board into download mode on this hardware.

## First run

With no credentials stored, PrintDrop raises its own access point:

| | |
|---|---|
| Network | `PrintDrop-Setup` |
| Password | `printdrop` |
| Setup page | `http://192.168.4.1` |

Join it, open the page, and set the Wi-Fi network and hostname under
**Settings**. The device reboots and reappears on the office network.

Credentials are stored in NVS and never enter the source tree.

### Provisioning over serial instead

Faster on a bench, and the only option if the AP is inconvenient:

```
wifi <ssid> <password>     join a network and reboot
hostname <name>            set the mDNS name and reboot
status                     show device state
forget                     clear Wi-Fi settings
help                       list commands
```

Send these as lines to the UART bridge (`COM5`) at 115200 baud.

## Finding it on the network

PrintDrop advertises itself over mDNS, so `http://printdrop.local` works on most
machines out of the box.

For a fixed address, either set a static IP under **Settings → Network**, or
reserve one on the DHCP server and point a DNS entry at it. With AdGuard Home,
add a DNS rewrite from e.g. `printdrop.office.lan` to the reserved address.

## Using it

- **Upload** — drag files anywhere onto the page, or use *Choose files*.
  Progress, transfer rate and ETA are shown per file. A 20 MB job takes roughly
  80 seconds; the card, not the network, is the bottleneck.
- **Browse** — grid or list view, sorted by name or size, with folder
  navigation. Print jobs get their own icon colour so they stand out.
- **Eject / refresh printer view** — forces the printer to re-read the card.
  Uploads do this automatically; the button is for when a printer needs nudging.

## Build environments

| Env | Purpose | Console |
|---|---|---|
| `printdrop` | The product — USB drive plus Wi-Fi web UI | UART0 (`COM5`) |
| `msc` | USB mass storage only, no networking | UART0 (`COM5`) |
| `ramdisk` | RAM-backed FAT12 volume; proves USB MSC without the SD card | UART0 (`COM5`) |
| `diag` | SPI speed sweep, card geometry, MBR dump, root listing | USB/JTAG (`COM11`) |
| `scan` | Pin health, line voltages, pin-permutation sweep | USB/JTAG (`COM11`) |

Troubleshooting order: `scan` when the card is not detected, `diag` when it
mounts but misbehaves, `ramdisk` to prove the USB path independently.

## Reflashing

Firmware that owns the native USB port replaces the USB-Serial-JTAG device, so
`esptool` cannot reach the board there. Every firmware watches UART0 for the
word `BOOTLOADER` and reboots into download mode:

```powershell
"BOOTLOADER" | Out-File -Encoding ascii COM5
```

On this board **RTS drives EN**, so opening or closing `COM5` resets the chip
and knocks it straight back out of download mode. Any tool used between the
hatch and the flash must hold RTS and DTR deasserted.

Manual fallback: hold **BOOT**, tap **RESET**, release **BOOT**.

## Layout

```
src/printdrop/    the product: storage arbitration, networking, HTTP API
src/diag/         SD and USB diagnostics
src/legacy/       USB mass storage only
src/common/       shared helpers
data/             web UI, flashed to LittleFS
ox-alpha/         investigation notes
```

## Partitions

4 MB flash, single app image — there is no room for two OTA slots.

| Partition | Offset | Size |
|---|---|---|
| `nvs` | `0x9000` | 20 KB |
| `app0` | `0x10000` | 2688 KB |
| `spiffs` (LittleFS) | `0x2B0000` | 1280 KB |
| `coredump` | `0x3F0000` | 64 KB |

## Gotchas

Three details stop the USB drive appearing at all, and none produce a useful
error message:

- **`ARDUINO_USB_MODE` must be `0`.** The stock board definition hard-codes `1`,
  which compiles `USBMSC` out entirely. Override through
  `board_build.extra_flags` — appending to `build_flags` only yields a macro
  redefinition.
- **`ARDUINO_USB_CDC_ON_BOOT` must be `0`.** It implies `ARDUINO_USB_ON_BOOT`,
  which makes the core call `USB.begin()` in `app_main()` *before* `setup()` —
  freezing the USB descriptor before the sketch can add its MSC interface.
- **SD cards must be identified at ≤400 kHz** before the clock is raised.

Full investigation notes: [`ox-alpha/FINDINGS.md`](ox-alpha/FINDINGS.md).

## Contributing

Build instructions, the branch model, and the hardware traps worth knowing about
are in [CONTRIBUTING.md](CONTRIBUTING.md). Bug reports are most useful with the
UART0 boot log and, for card problems, the output of the `diag` or `scan`
environment.

## Credits

Made by Akash P | CTO, [Kabani Tech Private Limited](https://kabanitech.com)

MIT licensed — see [LICENSE](LICENSE). The USB mass storage layer derives from
the `Esp32-USB-Stick` project for the M5Stack Cardputer.
