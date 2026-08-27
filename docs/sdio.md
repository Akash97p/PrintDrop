# SDIO 4-bit — `feat/sdio`

This branch migrates PrintDrop from the legacy 4-wire **SPI** SD driver
to the ESP32-S3's native **SDMMC host in 4-bit SDIO mode**.

## Why

The SPI path caps at ~910 KB/s raw, 485 KB/s USB read, 248 KB/s USB write.
A 20 MB `gcode` takes ~80 s to upload — the card, not Wi-Fi, is the bottleneck.
The same card on the same breakout at the same 20 MHz in SDIO 4-bit sustains
~3 500 KB/s raw, ~3 200 KB/s USB read, ~2 000 KB/s USB write; the same job
lands in ~6 s. At 40 MHz the SDHC high-speed ceiling is ~6 MB/s raw.

See [`docs/hardware.md`](hardware.md#sdio-clocks--featsdio-projected) for the
sweep and [`docs/architecture.md`](architecture.md#measured-performance) for the
bottleneck note.

## Wiring

SDIO reuses the four SPI pins plus two new data lines, so an SPI-wired board
migrates with two jumpers:

| Signal | GPIO | SDIO notes |
|--------|------|------------|
| CLK    | 40   | |
| CMD    | 14   | 10 kΩ pull-up to 3V3 |
| D0     | 39   | 10 kΩ pull-up to 3V3 |
| D1     | 12   | 10 kΩ pull-up to 3V3 |
| D2     | 13   | 10 kΩ pull-up to 3V3 |
| D3     | 15   | 10 kΩ pull-up to 3V3 |

Legacy SPI (4 wires): `CS=12 MISO=39 MOSI=14 CLK=40`.

SDIO **requires a 3.3 V-native microSD breakout** — no AMS1117/LC125. Modules
with an AMS1117 reference their pull-ups to the 5 V rail and drive the ESP32
pins above the 3.6 V absolute maximum (see `docs/hardware.md` power section).
The breakout must have the four pull-ups above; the ESP32's internal
pull-ups are weaker and not sufficient for SDIO. Bring-up can use 1-bit mode
(`SDMMC_WIDTH=1`, only CLK/CMD/D0) before wiring D1-D3.

```
# feat/sdio — SDIO is the default
pio run -e printdrop        # SDIO 4-bit @ 40 MHz (falls back automatically)
pio run -e diag_sdio        # SDIO bus width + throughput sweep

# legacy — SPI without re-wiring
pio run -e printdrop_spi    # SPI @ 20 MHz
pio run -e diag             # SPI speed sweep
```

## Software

### `src/printdrop/config.h`

* Keeps the SPI pins (`SD_*_PIN`, `SD_SPI_FREQ`) for the `printdrop_spi`
  environment.
* Adds the SDIO pins (`SDMMC_*_PIN`), bus width (`SDMMC_WIDTH`, 1 or 4) and
  clock (`SDMMC_FREQ`, Hz, default 40 MHz). All are overridable from
  `platformio.ini` `build_flags`.

### `src/printdrop/storage.cpp`

* `#ifdef USE_SDIO` selects `SD_MMC` + `sdmmc_read_sectors`/`sdmmc_write_sectors`
  via the SDMMC host; otherwise `SPI` + `SD` (`SDFS`).
* The Arduino 2.0 `SDMMCFS` does not expose `readRAW`/`sectorSize`/`numSectors`,
  so the branch accesses the underlying `sdmmc_card_t` (via a private-member
  hack `SDMMCHack::_card`) and calls the IDF `sdmmc_*` sector API directly.
  A future core that adds those accessors will let the hack be removed.
* `mountCard()` for SDIO sets the pins with `SD_MMC.setPins()`, then walks a
  frequency ladder (`SDMMC_FREQ` → 20 → 10 → 4 → 1 MHz), calling
  `SD_MMC.begin("/sdcard", mode1bit, false, freqKhz)` and verifying sector 0's
  `55 AA` MBR signature before trusting the bus. The SPI path retains its
  400 kHz cold-identification ladder.
* `spiFrequency()` is kept as an alias; new code should call `busFrequency()`
  / `busWidth()` / `busMode()` (`"sdio-4bit"`, `"sdio-1bit"`, `"spi"`).
* The three arbitration rules (`one mutex`, `withdraw before writing`, `remount
  when host writes`) and the `2 s` MSC lock timeout are unchanged — the bus
  is an implementation detail to the rest of the firmware.

### `src/printdrop/web.cpp` + `main.cpp`

* `web.cpp` introduces `SD_FS` (`SD_MMC` or `SD`) so `open`/`exists`/`remove`
  etc. are bus-agnostic, and extends `/api/status` with `busHz`/`busMode`/
  `busWidth` (`spiHz` is kept for compatibility).
* `main.cpp` banner and `status` console command report the active bus.

### `platformio.ini`

* `[env]` adds the six SDIO pin definitions.
* `[env:printdrop]` defines `USE_SDIO` + `SDMMC_WIDTH=4` (now SDIO).
* `[env:printdrop_spi]` is the SPI legacy snapshot (`ARDUINO_USB_MODE=0`).
* `[env:diag_sdio]` is the SDIO bring-up environment.

## Website

`website/src/app/page.tsx` on this branch shows SDIO figures (3 200/2 000 KB/s,
~6 s for 20 MB) and a **This branch** note; the SPI figures remain in the
paragraph as the `main` baseline and in `hardware.md`. After merge to `main`,
the site will ship the same numbers.

## Testing plan

1. `pio run -e diag_sdio -t upload` — verify the probe passes at 20 MHz 1-bit
   before wiring D1-D3, then at 20 MHz 4-bit, then at 40 MHz 4-bit.
2. `pio run -e printdrop -t upload` — check `SD bus: SDIO 4-bit ...` banner,
   `status` command, and that the card enumerates.
3. Upload 20 MB `benchy.gcode` via `http://printdrop.local` — expect ~6 s not
   ~80 s; verify SHA-256 on the printer host matches.
4. During upload, confirm the printer's file list withdraws and reappears
   without a reboot, and that a concurrent USB read does not stall (short mutex
   timeout).
5. `pio run -e printdrop_spi` — regression: SPI still enumerates on the same
   hardware with only the four original wires.

## Rollback

SPI is not removed. `pio run -e printdrop_spi` builds the `main` driver
without re-wiring, and `git checkout main` restores the SPI-default branch.
