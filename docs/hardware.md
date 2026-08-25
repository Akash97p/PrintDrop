# Hardware

## The board, as measured

Read off the chip with `esptool.py flash_id` rather than trusted from the
silkscreen — the two disagreed:

| Property | Value |
|---|---|
| Chip | ESP32-S3, revision **v0.2** |
| Flash | **4 MB**, quad (QIO), manufacturer `0xC8` (GigaDevice), device `0x4016` |
| PSRAM | **none** (`ESP.getPsramSize()` = 0; esptool reports only WiFi/BLE) |
| Arduino core | 2.0.11 (`framework-arduinoespressif32` 3.20011.230801), IDF v4.4.5 |
| PlatformIO platform | espressif32 6.4.0 |

This is a **4 MB, no-PSRAM** board — *not* the N8R8 that the stock
`esp32-s3-devkitc-1` board definition assumes. The partition table and
`board_upload.flash_size` are set accordingly; see
[architecture.md](architecture.md#partitions).

## Serial ports

The DevKitC-1 exposes two USB connectors, and both are used:

| Port | What it is |
|---|---|
| `COM5` | CP2102 USB-UART bridge (`10C4:EA60`) → UART0. The console. |
| `COM11` | Native USB. Runs the ROM's USB-Serial-JTAG until firmware claims it for TinyUSB. |

Once PrintDrop is running, the native port is the mass storage device and all
logging goes to the UART bridge.

## SD wiring

| Signal | GPIO |
|--------|------|
| CS     | 12   |
| MISO   | 39   |
| MOSI   | 14   |
| CLK    | 40   |

Set through `build_flags` in `platformio.ini`, with matching defaults in
`src/printdrop/config.h`.

## Power: use 3V3

The module used here is the 6-pin microSD type carrying an **AMS1117**
regulator and an **LC125** (74LVC125) quad buffer. **Power it from 3V3.**

It will also run from 5 V, and appears to work — but its pull-up resistors
reference the raw `VCC` rail rather than the regulated output, so feeding it 5 V
drives the SPI lines toward 5 V. Measured high-impedance with the ADC
attenuation pinned:

```
GPIO12 (CS  ): hi-Z 4981 mV (raw 4095)  -> OVER RANGE, above 3.3V
GPIO14 (MOSI): hi-Z 4981 mV (raw 4095)  -> OVER RANGE, above 3.3V
```

A raw count of 4095 is the ADC pegged at full scale, so the exact figure is not
trustworthy — but the direction is unambiguous. On 3V3 the same pins read
145 mV and 65 mV high-impedance, and 2770/2857 mV with the internal pull-up.

The ESP32-S3 absolute maximum on a GPIO is VDD + 0.3 V = 3.6 V. Above that the
clamp diodes conduct and inject current into the 3.3 V rail.

If you have a module that genuinely needs 5 V, either use a 3.3 V-native
microSD breakout instead (no regulator, no level shifter), or bridge the
AMS1117's input and output pins so the whole board runs at 3.3 V.

## Verified SPI clocks

The `diag` environment mounts the card at each clock, verifies it by re-reading
sector 0 against a 400 kHz reference, and times a 64 KB read — so the firmware's
clock is chosen from measurement rather than assumed:

```
  400000 Hz : OK    47 KB/s        10000000 Hz : OK   668 KB/s
 1000000 Hz : OK   112 KB/s        16000000 Hz : OK   840 KB/s
 4000000 Hz : OK   368 KB/s        20000000 Hz : OK   910 KB/s
 8000000 Hz : OK   585 KB/s        25000000 Hz : OK   910 KB/s
```

Every speed verified clean; throughput saturates around 20 MHz at ~910 KB/s.
`SD_SPI_FREQ` is 20 MHz, inside the verified range with margin.

**This measures the sustained clock, not cold initialisation.** See
[bugs.md](bugs.md#sd-cards-must-be-identified-at-400-khz) — the card must be
identified slowly before the clock is raised, regardless of what it sustains
afterwards.

## Card under test

```
Card type    : SDHC/SDXC (3)
Card size    : 31914983424 bytes (30436 MB)
Sector size  : 512      Sector count : 62333952
Partition 1  : type=0x0C  startLBA=2048  sectors=62330880   -> FAT32
Boot signature: 55 AA
```

FAT32 is required. exFAT will not mount on the ESP32 side, even though Windows
would read it happily over USB.
