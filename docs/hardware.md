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

### SDIO 4-bit — `feat/sdio` (recommended, 6 wires)

| Signal | GPIO | Notes |
|--------|------|-------|
| CLK    | 40   | |
| CMD    | 14   | 10 k pull-up to 3V3 |
| D0     | 39   | 10 k pull-up to 3V3 |
| D1     | 12   | 10 k pull-up to 3V3 |
| D2     | 13   | 10 k pull-up to 3V3 |
| D3     | 15   | 10 k pull-up to 3V3 |

Reuses the four SPI pins plus two new data lines — an SPI-wired board migrates by adding D2/D3 and pull-ups. Requires a **3.3 V-native microSD breakout** (no AMS1117/LC125) with the pull-ups listed above. CLK/CMD/D0 can be validated in 1-bit mode (`SDMMC_WIDTH=1`) before wiring D1-D3. Set through `build_flags` in `platformio.ini`, with matching defaults in `src/printdrop/config.h` (`SDMMC_*_PIN`, `SDMMC_FREQ`, `SDMMC_WIDTH`).

```
# feat/sdio — SDIO is the default
pio run -e printdrop        # SDIO 4-bit @ 40 MHz
pio run -e diag_sdio        # SDIO throughput sweep

# legacy — SPI without re-wiring
pio run -e printdrop_spi    # SPI @ 20 MHz
pio run -e diag             # SPI speed sweep
```

### SPI — `main` legacy (4 wires)

| Signal | GPIO |
|--------|------|
| CS     | 12   |
| MISO   | 39   |
| MOSI   | 14   |
| CLK    | 40   |

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

## SDIO clocks — `feat/sdio` (projected)

The `diag_sdio` environment sweeps the SDMMC host at 10/20/40 MHz in both 1-bit
and 4-bit modes, verifying each step with the same sector-0 probe. On jumper
wiring the ESP32-S3 SDMMC host sustains 20 MHz cleanly; 40 MHz wants short,
equal-length wires and solid pull-ups. Expected deltas over SPI:

| Bus | Clock | Raw SD | USB read | USB write | 20 MB upload |
|-----|-------|--------|----------|-----------|--------------|
| SPI 1-bit | 20 MHz | ~910 KB/s | 485 KB/s | 248 KB/s | ~80 s |
| SDIO 1-bit | 20 MHz | ~1 800 KB/s | ~1 200 KB/s | ~900 KB/s | ~22 s |
| **SDIO 4-bit** | **20 MHz** | **~3 500 KB/s** | **~3 200 KB/s** | **~2 000 KB/s** | **~6 s** |
| SDIO 4-bit | 40 MHz | ~6 000 KB/s | ~5 000 KB/s * | ~3 500 KB/s * | ~4 s |

\* SDIO 4-bit @ 40 MHz is the SDHC high-speed ceiling — jumper wires may need to stay at 20 MHz. Re-measure after wiring. `SDMMC_FREQ` defaults to 40 MHz; step down to 20 MHz if the probe fails.

1-bit SDIO is already ~2× SPI and useful for bring-up (only CLK/CMD/D0 needed);
4-bit multiplies that by ~4. See [architecture.md](architecture.md#measured-performance) for the bottleneck note.

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
