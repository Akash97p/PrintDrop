# Contributing to PrintDrop

PrintDrop is embedded firmware with a browser front end, and most of its
interesting bugs only appear with real hardware attached. This document covers
what you need to build it, how the repository is organised, and the handful of
traps that have already cost time.

## What you need

**Hardware**

- An ESP32-S3 board. The tree is configured for an **ESP32-S3-DevKitC-1 with
  4 MB flash and no PSRAM**; other variants need the partition table and
  `board_upload.flash_size` adjusted.
- An SD or microSD breakout wired for SPI, powered from **3V3**.
- A **FAT32** card. exFAT will not mount.
- Both USB ports connected — the native port carries the mass storage device,
  the UART bridge carries the console.

**Toolchain**

- [PlatformIO](https://platformio.org/) (CLI or the VS Code extension).
- Nothing else. There is no Node toolchain; the web UI is plain files.

```bash
pio run -e printdrop              # build firmware
pio run -e printdrop -t upload    # flash firmware
pio run -e printdrop -t uploadfs  # flash the web UI into LittleFS
```

Changing anything under `data/` needs `uploadfs`, not `upload`.

## Repository layout

```
src/printdrop/    the product
  main.cpp        startup, serial console
  storage.cpp     SD card and USB mass storage arbitration
  net.cpp         Wi-Fi provisioning, mDNS, static IP
  web.cpp         HTTP server and JSON file API
src/diag/         SD and USB diagnostics
src/legacy/       USB mass storage only, no networking
src/common/       shared helpers
data/             web UI, flashed to LittleFS
docs/             architecture, hardware, bugs, flashing notes
assets/           branding
site/             GitHub Pages source
```

## Branches and commits

`main` is the stable line and `dev` is the integration branch. Neither is
developed on directly.

Branch from an up-to-date `dev`:

| Prefix | For |
|---|---|
| `feature/` | new capability |
| `fix/` | defect |
| `docs/` | documentation |
| `test/` | test-only work |
| `chore/` | tooling and maintenance |

Commit subjects follow [Conventional Commits](https://www.conventionalcommits.org/)
in the imperative mood — `feat: add rename endpoint`, not `added renaming`.
Explain *why* in the body when the reason is not obvious from the diff; the
firmware is full of decisions that look arbitrary until you know what broke.

Merge a verified branch into `dev` with `git merge --no-ff`, then delete the
topic branch.

## Things that will catch you out

These are documented because each one cost real debugging time, and none of
them produce a useful error message. Longer write-ups with the supporting
evidence are in [docs/bugs.md](docs/bugs.md).

**`ARDUINO_USB_MODE` must be `0`.** The stock `esp32-s3-devkitc-1` board
definition hard-codes `1`, which puts the native port on the hardware
USB-Serial-JTAG peripheral and compiles `USBMSC` out entirely — its whole body
sits behind `#if CONFIG_TINYUSB_MSC_ENABLED`. Override it through
`board_build.extra_flags`; appending `-D ARDUINO_USB_MODE=0` to `build_flags`
only produces a macro redefinition with undefined precedence.

**`ARDUINO_USB_CDC_ON_BOOT` must be `0`.** `USB.h` derives
`ARDUINO_USB_ON_BOOT` from it, and the core's `app_main()` then calls
`USB.begin()` *before* `setup()` runs. After that `ESPUSB::PID()` silently
returns early and `tinyusb_enable_interface()` refuses with *"TinyUSB has
already started"* — so the descriptor is frozen before the sketch can add its
MSC interface. Enabling a USB serial console is what stops the disk appearing.

**SD cards must be identified at 400 kHz or less.** The SD specification allows
the clock to rise only after CMD0/CMD8/ACMD41 have completed. `SD.begin()` runs
that whole sequence at whatever frequency it is handed, so calling it directly
at 20 MHz fails on a cold card with `Card Failed! cmd: 0x00`. Mount slow, then
re-mount fast.

**Reflashing needs the escape hatch.** Firmware that owns the native USB port
replaces the USB-Serial-JTAG device, so `esptool` cannot reach the board there.
Every firmware watches UART0 for the word `BOOTLOADER` and reboots into download
mode:

```powershell
"BOOTLOADER" | Out-File -Encoding ascii COM5
```

On the DevKitC-1 **RTS drives EN**, so opening *or closing* the UART port resets
the chip and knocks it straight back out of download mode. Any tool used between
the hatch and the flash must hold RTS and DTR deasserted. Manual fallback: hold
**BOOT**, tap **RESET**, release **BOOT**.

**Power SD modules from 3V3.** Modules with an AMS1117 regulator also run on
5 V, but their pull-ups then reference the 5 V rail and drive the ESP32's GPIOs
above 3.3 V — outside the SoC's absolute maximum of VDD + 0.3 V.

## Touching the storage layer

`src/printdrop/storage.cpp` is the part to be careful with — read
[docs/architecture.md](docs/architecture.md#sharing-the-card) first. A USB host caches
the FAT, so writing to the card behind its back corrupts the filesystem. Three
rules hold it together:

1. Every SD access, from either side, is serialised on one mutex.
2. The media is withdrawn from the USB host before the ESP32 modifies the card,
   and re-presented afterwards so the host re-reads its allocation table.
3. MSC write callbacks flag that the host has touched sectors, and FATFS is
   remounted before the ESP32 trusts its own view again.

MSC callbacks run on the TinyUSB task. They must take the mutex with a short
timeout and fail the transfer rather than block, or a slow upload will stall the
printer mid-job. If you add a code path that touches the card, it goes through
`storage::Guard`.

## Working on the web UI

`data/` is served from a 1280 KB LittleFS partition on a device with no internet
access, so:

- **No external requests.** No CDN, no web fonts, no remote images. System font
  stacks only.
- **No build step.** Plain HTML, CSS and vanilla JS that run as authored.
- **Inline SVG** for icons — no icon fonts.
- Keep the three files well under 150 KB uncompressed.
- Do not poll the device faster than every 5 seconds; it is single-threaded and
  busy serving the printer.

Opening `data/index.html` straight from disk falls back to mock data, so layout
and styling can be iterated in a normal browser without flashing anything.

## Testing a change

There is no automated test suite — the device is the test rig. Before opening a
pull request, confirm on hardware:

- The card still enumerates as a USB drive and the filesystem is intact.
- A file uploaded over Wi-Fi appears to the USB host without a reboot.
- An upload during an active USB session neither corrupts the card nor stalls
  the host.
- The board still reaches download mode via the `BOOTLOADER` hatch.

Three diagnostic environments exist for when something is wrong:

| Env | Use it when |
|---|---|
| `scan` | the card is not detected at all — pin health, line voltages, pin-permutation sweep |
| `diag` | the card mounts but misbehaves — SPI speed sweep, geometry, MBR dump |
| `ramdisk` | proving the USB path works independently of the SD card |

## Reporting a problem

Include:

- Board and flash configuration — `esptool.py flash_id` output is ideal.
- Which build environment you flashed.
- The UART0 console log from boot, not just the failing moment.
- For card problems, the output of the `diag` or `scan` environment.
- For UI problems, the browser and any console errors.

## Licence

Contributions are accepted under the [MIT Licence](LICENSE), the same terms as
the rest of the project.
