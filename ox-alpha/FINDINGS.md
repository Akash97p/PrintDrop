# ESP32-S3 USB Stick — Findings

Porting `Esp32-USB-Stick` from an **M5Stack Cardputer** to a bare
**ESP32-S3-DevKitC-1**, and getting the SD card to enumerate as a USB drive.

Working directory is accessed from WSL; all flashing and serial work goes
through the Windows PlatformIO install (`%USERPROFILE%\.platformio`), because
the serial ports are Windows COM ports.

---

## 1. Hardware as actually measured

Read off the chip with `esptool.py flash_id`, not from the board silkscreen:

| Property | Value |
|---|---|
| Chip | ESP32-S3, revision **v0.2** |
| Flash | **4 MB**, quad (QIO), manufacturer `0xC8` (GigaDevice), device `0x4016` |
| PSRAM | **none** (`ESP.getPsramSize()` = 0; esptool reports only WiFi/BLE) |
| MAC | `98:88:E0:xx:xx:xx` |
| Arduino core | 2.0.11 (`framework-arduinoespressif32` 3.20011.230801), IDF v4.4.5 |
| PlatformIO platform | espressif32 6.4.0 |

This is a **4 MB, no-PSRAM** board — *not* the N8R8 the stock board definition
assumes.

### Serial ports

| Port | What it is |
|---|---|
| `COM5` | CP2102 USB-UART bridge (`10C4:EA60`) → UART0 |
| `COM11` | Native USB, running the ROM's USB-Serial-JTAG (`303A:1001`) |

---

## 2. Why no USB device ever appeared

Four independent defects. Any one of them alone is enough to produce exactly the
symptom reported — board resets, nothing enumerates.

### 2.1 `setup()` never finished — blocking wait on Cardputer hardware  *(root cause)*

`src/main.cpp` called `displayWelcome()` before touching USB, and that function
ends in:

```cpp
while (true) {
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange()) break;   // never true
    delay(5);
}
```

A DevKitC-1 has no Cardputer keyboard, so `isChange()` never returns true. The
loop spins forever, `USB.begin()` is never reached, and the native port stays in
its ROM USB-Serial-JTAG role. That is precisely what the host saw: `COM11`
enumerating as *"USB JTAG/serial debug unit"* rather than as a disk.

`M5Cardputer.begin()` also drives a display and keyboard matrix that do not
exist on this board.

**Fix:** dropped M5Unified/M5Cardputer entirely and rewrote `main.cpp` for a
headless board. All status now goes to serial.

### 2.2 `ARDUINO_USB_MODE=1` — TinyUSB compiled out

The PlatformIO board definition for `esp32-s3-devkitc-1` hard-codes:

```json
"extra_flags": [ "-DARDUINO_ESP32S3_DEV", "-DARDUINO_USB_MODE=1", ... ]
```

`ARDUINO_USB_MODE=1` means *"use the hardware USB-Serial-JTAG peripheral"*. In
that mode `USBMSC.h` is an empty shell — its whole body sits behind
`#if CONFIG_TINYUSB_MSC_ENABLED`, and the TinyUSB stack that serves it is never
attached to the USB PHY. USB mass storage cannot work at `ARDUINO_USB_MODE=1`.

The commented-out line in the original `platformio.ini` would not have helped —
it set the same wrong value:

```ini
; -D ARDUINO_USB_MODE=1
```

`ARDUINO_USB_MODE=0` is required. Note that **appending** `-D ARDUINO_USB_MODE=0`
to `build_flags` is not enough: the board's flag is emitted first and you get a
macro-redefinition warning with undefined precedence. The board's list has to be
replaced via `board_build.extra_flags`.

### 2.3 Partition table sized for 8 MB flash on a 4 MB chip

The stock board definition uses `default_8MB.csv`, and the repo's own
`partitions.csv` (which was never actually referenced from `platformio.ini`)
runs to `0x800000`. Both place partitions past the end of a 4 MB chip.

`platformio.ini` also declared `board_upload.flash_size = 4MB` while leaving the
8 MB partition table in place — an inconsistent pair.

**Fix:** `board_build.partitions = huge_app.csv` (3 MB app, fits 4 MB) plus
explicit `board_upload.flash_size` / `maximum_size`.

### 2.4 PSRAM configured on a board that has none

```ini
board_build.arduino.memory_type = qio_qspi
board_build.psram = qspi
```

There is no PSRAM on this chip. Removed the `psram` setting; `qio_qspi` is the
correct SDK variant for quad flash / no PSRAM and was kept.

---

## 3. Why the SD card was invisible

Diagnostic firmware (`src/sd_diag.cpp`) reported the card completely silent on
the documented pins:

```
MISO with pull-up   : 1
MISO with pull-down : 0        -> floating, nothing driving it
CMD0 (GO_IDLE_STATE) response: 0x00     (a real card answers 0x01)
SD.begin() failed at 400 kHz, 1, 4, 10, 20 and 25 MHz
```

A second firmware (`src/sd_scan.cpp`) bit-bangs the SD init sequence so it can
drive arbitrary GPIOs, and tests each pin's health. It found:

```
GPIO12 (CS  ): drive HIGH -> 1, drive LOW -> 0   OK
GPIO39 (MISO): drive HIGH -> 1, drive LOW -> 0   OK
GPIO14 (MOSI): drive HIGH -> 0, drive LOW -> 0   *** FAULT ***
GPIO40 (CLK ): drive HIGH -> 1, drive LOW -> 0   OK
```

**GPIO14 could not be driven high.** That is the signature of another driver
holding the line: the SD card's own DO (data out) pin was connected to GPIO14
and winning against the ESP32's output.

**Cause: MISO and MOSI were swapped** — MISO was on GPIO14 and MOSI on GPIO39,
the mirror image of the documented pinout. Corrected to:

| Signal | GPIO |
|---|---|
| CS | 12 |
| MISO | 39 |
| MOSI | 14 |
| CLK | 40 |

The card itself was never at fault, and was never actually reachable to be
tested — with MISO/MOSI crossed no card of any kind could have responded.

### 3.1 After the rewire: the fault followed the wire

Re-running the scanner with MISO/MOSI corrected:

| Pin | Before rewire | After rewire |
|---|---|---|
| GPIO14 | **stuck LOW** (was MISO) | OK (now MOSI) |
| GPIO39 | OK (was MOSI) | **driven LOW externally** (now MISO) |

GPIO14 is healthy again, confirming the swap was real and is corrected. But the
hard-low condition **moved to GPIO39 along with the MISO wire**. The ESP32 is
not at fault on either pin; something on the MISO line is holding it low.

Line voltages with the internal pull-up enabled (ADC2 covers GPIO11–20, so only
CS and MOSI can be measured; GPIO39/40 are digital-only):

```
GPIO12 (CS  ): 2612 mV  -> free
GPIO14 (MOSI): 2854 mV  -> free
GPIO39 (MISO): no ADC on this pin - digital only  (reads hard LOW)
GPIO40 (CLK ): no ADC on this pin - digital only  (floating)
```

CS and MOSI pull up cleanly, so those lines are unloaded. MISO is the only one
being pulled down, and it is pulled below the digital threshold even against
the internal pull-up.

**Interpretation.** A chip with no VCC clamps any pin you pull up down to
roughly 0.6 V through its ESD protection diode. A held-low MISO with the other
three lines free is the textbook signature of an **unpowered SD module** — or
of the MISO jumper sitting one pin off, on GND.

The scanner now includes `TEST 2b: LINE VOLTAGES` to separate these: ~0 V is a
hard short to ground, ~0.3–0.9 V is a diode clamp (no VCC), ~3.3 V is free.
Moving the MISO wire to GPIO12 temporarily makes it measurable, since the
scanner re-runs every 10 seconds.

### 3.2 First cause: the module was browned out

The module is the 6-pin microSD type carrying an **AMS1117** regulator and an
**LC125** (74LVC125) quad buffer, and it was being powered from **3V3**.

The AMS1117 is a linear regulator with roughly 1.1–1.3 V of dropout. Fed 3.3 V
it cannot produce 3.3 V — its output sits somewhere under 2.2 V. Both the LVC125
buffer and the card itself were browned out, which is why MISO sat clamped low
while the three ESP32-driven lines read free.

**Fix: move the module's VCC to 5V/VIN/VBUS.** The signal lines stay 3.3 V, which
is what the LVC125 is there to handle.

Immediately after the change:

```
CS=12 MISO=39 MOSI=14 CLK=40 -> R1=0x01  *** CARD FOUND ***
MOUNTED at 400000 Hz
  type=3 size=31914983424 bytes sectorSize=512 numSectors=62333952
```

> These 6-pin modules are labelled as though 3.3 V works. With an AMS1117 on
> board it does not. If it has a regulator, feed it 5 V.

### 3.3 The 5V detour, and what it actually showed

Powering the module from 5V made the card work, but it also put the **signal
lines above 3.3 V**, which the ESP32-S3 cannot accept. Measured high-impedance
(nothing pulled up by the ESP32), with the ADC attenuation pinned:

```
GPIO12 (CS  ): hi-Z 4981 mV (raw 4095), pullup 4981 mV -> OVER RANGE, above 3.3V
GPIO14 (MOSI): hi-Z 4981 mV (raw 4095), pullup 4981 mV -> OVER RANGE, above 3.3V
GPIO39 (MISO): no ADC on this pin
GPIO40 (CLK ): no ADC on this pin
```

A raw count of 4095 is the ADC pegged at full scale, so the true voltage is
"at least above the ADC range" — the exact figure is not trustworthy, but the
direction is unambiguous. For comparison, on 3V3 the same pins read 2606 mV and
2863 mV *with the internal pull-up engaged*; they now saturate with no pull-up
at all. All four lines also report `pulldown=1 pullup=1`, i.e. actively held
high against an internal pull-down.

**Cause.** This module's pull-up resistors reference the raw `VCC` rail rather
than the AMS1117's regulated 3.3 V output. Feeding `VCC` 5 V therefore pulls the
SPI lines toward 5 V. The ESP32-S3 absolute maximum on a GPIO is VDD + 0.3 V
= 3.6 V; its clamp diodes conduct and inject current into the 3.3 V rail.

**The module is in a bind:** on 3V3 the AMS1117 browns out (§3.2), on 5V its
pull-ups over-drive the ESP32. Neither is correct as wired.

Shortly after this the card stopped answering CMD0 entirely — the permutation
sweep finds nothing, while `TEST 1` still shows GPIO12/14/40 driving cleanly, so
the ESP32 outputs themselves appear undamaged.

**Options, best first:**

1. **Use a 3.3 V-native microSD breakout** — no regulator, no level shifter,
   VCC straight to 3V3. These are made for 3.3 V micros and need no workaround.
2. **Bypass the AMS1117** on this module by bridging its input and output pins,
   then power VCC from **3V3**. Everything then runs at 3.3 V and the pull-ups
   reference 3.3 V too. Requires soldering.
3. Keep 5 V only with proper level shifting on all four lines. More parts than
   options 1 or 2 are worth.

**Resolution: VCC returned to 3V3, and the card works there.** With the module
back on 3.3 V the lines measure correctly in range and the card responds:

```
GPIO12 (CS  ): hi-Z  145 mV (raw  165), pullup 2770 mV -> free / not driven
GPIO14 (MOSI): hi-Z   65 mV (raw   75), pullup 2857 mV -> free / not driven
CS=12 MISO=39 MOSI=14 CLK=40 -> R1=0x01  *** CARD FOUND ***
MOUNTED  type=3 size=31914983424 sectorSize=512 numSectors=62333952
```

Neither the card nor the ESP32 was damaged by the 5 V period.

**Correcting §3.2.** The AMS1117 dropout explanation was wrong. The regulator
does have ~1.1 V of dropout, but the module demonstrably works on 3.3 V, so that
was not what kept the card silent. The likelier cause of the original 3.3 V
failure was a marginal connection reseated during the rewiring. The 5 V move
was unnecessary, and 3V3 is the correct and in-spec configuration.

**Run this module on 3V3.** 5 V works but over-drives the ESP32's GPIOs.

### 3.4 Verified SPI speeds

`SPI SPEED SWEEP` in `sd_diag.cpp` mounts at each clock and verifies it by
re-reading sector 0 against a 400 kHz reference, then times a 64 KB read — so
the firmware's clock is chosen from measurement rather than assumed:

```
  400000 Hz : OK    47 KB/s        10000000 Hz : OK   668 KB/s
 1000000 Hz : OK   112 KB/s        16000000 Hz : OK   840 KB/s
 4000000 Hz : OK   368 KB/s        20000000 Hz : OK   910 KB/s
 8000000 Hz : OK   585 KB/s        25000000 Hz : OK   910 KB/s
```

Every speed verified clean; throughput saturates around 20 MHz at ~910 KB/s.
`main.cpp` uses 20 MHz (`SD_SPI_FREQ`), inside the verified range with margin.

**Caveat this sweep initially hid.** It measures the *sustained* clock, not cold
initialisation. The 400 kHz pass identifies the card, and the card stays
initialised for the rest of the sweep, so later passes only re-mount an
already-awake card. Building `main.cpp` to call `SD.begin(cs, spi, 20000000)`
directly therefore failed on a cold card with `Card Failed! cmd: 0x00`.

The SD specification requires the identification sequence (CMD0/CMD8/ACMD41) to
run at **400 kHz or below**; the clock may only be raised afterwards. `mountSD()`
now mounts at 400 kHz first, then re-mounts at full speed, stepping down a
ladder (20 → 10 → 4 → 1 MHz) and verifying the MBR signature at each step before
trusting the link.

Card as reported by the driver:

```
Card type    : SDHC/SDXC (3)
Card size    : 31914983424 bytes (30436 MB)
Sector size  : 512      Sector count : 62333952
Partition 1  : type=0x0C  startLBA=2048  sectors=62330880   -> FAT32
Boot signature: 55 AA
Root: System Volume Information/, projects/
```

---

## 4. The USB descriptor problem

After fixing the build flags, the RAM-disk self-test (`src/msc_ramdisk.cpp`,
below) enumerated — but as **CDC only**, with no mass-storage interface:

```
USB\VID_303A&PID_1001\9888E09D1230              USB Composite Device
USB\VID_303A&PID_1001&MI_00\...   "USB JTAG/serial debug unit (Interface 0)"
USB\VID_303A&PID_1001&MI_01\...   "USB Serial Device (COM27)"
```

Only `MI_00`/`MI_01` — the two CDC interfaces. No `MI_02` for MSC. Windows was
still labelling `MI_00` as *"USB JTAG/serial debug unit"*, which it is not any
more: Windows had cached the descriptor and driver bindings for
`VID_303A:PID_1001` from the ROM's USB-Serial-JTAG device, and served the stale
layout when the same VID/PID came back with a different configuration.

Setting a distinct PID with `USB.PID()` **had no effect** — the device still
enumerated as `PID_1001`, and MSC was still absent. Both failures have the same
cause.

### 4.1 Root cause: `USB.begin()` runs before `setup()`

`USB.h`:

```c
#define ARDUINO_USB_ON_BOOT (ARDUINO_USB_CDC_ON_BOOT|ARDUINO_USB_MSC_ON_BOOT|ARDUINO_USB_DFU_ON_BOOT)
```

the core's `main.cpp`:

```c
extern "C" void app_main() {
#if ARDUINO_USB_CDC_ON_BOOT && !ARDUINO_USB_MODE
    Serial.begin();
#endif
#if ARDUINO_USB_ON_BOOT && !ARDUINO_USB_MODE
    USB.begin();                 // <-- before setup()
#endif
    initArduino();
    xTaskCreateUniversal(loopTask, ...);   // loopTask calls setup()
}
```

So **`ARDUINO_USB_CDC_ON_BOOT=1` implies `USB.begin()` at boot.** That sets
`ESPUSB::_started`, and every configuration setter is a silent no-op afterwards:

```c
bool ESPUSB::PID(uint16_t p){
    if(!_started){ pid = p; }
    return !_started;          // returns false, nobody checks
}
```

`tinyusb_enable_interface()` likewise refuses once the stack is up:

```c
log_e("TinyUSB has already started! Interface %s not enabled", ...);
```

The USB descriptor was therefore frozen — CDC only, stock PID — before a single
line of `setup()` executed. Turning CDC on to keep a debug console is exactly
what stopped the disk from appearing.

The stock PID is `0x1001` because PlatformIO derives `USB_VID`/`USB_PID` from
the board definition's `hwids`, which for `esp32-s3-devkitc-1` is
`[["0x303A","0x1001"]]` — the same ID as the ROM's USB-Serial-JTAG device, which
is why Windows kept serving the cached JTAG descriptor.

**Fix:** build the TinyUSB environments with `ARDUINO_USB_CDC_ON_BOOT=0`. Then
nothing calls `USB.begin()` at boot, and `setup()` controls the whole sequence:
register MSC → set VID/PID/class → `msc.begin()` → `USB.begin()`. Console output
moves to UART0 on `COM5`, which is always present anyway.

| Firmware | VID:PID | Device class |
|---|---|---|
| `msc` (SD reader) | `303A:4001` | `TUSB_CLASS_UNSPECIFIED` (single-function MSC) |
| `ramdisk` (self-test) | `303A:4002` | `TUSB_CLASS_UNSPECIFIED` |

`TUSB_CLASS_UNSPECIFIED` replaces the core's default IAD composite class: with
one interface there is no composite to announce, and the host classifies from
the interface descriptor.

---

## 5. Flashing this specific board

### The auto-reset circuit on COM5 is only half wired

`esptool` on `COM5` always failed with:

```
A fatal error occurred: Failed to connect to Espressif device:
Wrong boot mode detected (0x8)! The chip needs to be in download mode.
```

Driving the reset lines by hand shows why:

```
DTR/RTS toggled -> ESP-ROM:esp32s3-20210327
                   rst:0x1 (POWERON), boot:0x8 (SPI_FAST_FLASH_BOOT)
```

The chip **does** reset (so RTS → EN is connected) but comes up in normal flash
boot, meaning **DTR → GPIO0 is not connected**. `COM5` can therefore never put
this board into download mode on its own, no matter what esptool options are
used. This is a board wiring limitation, not a software problem.

### What that means in practice

| Situation | How to flash |
|---|---|
| Firmware with `ARDUINO_USB_MODE=1` (`diag`, `scan`) | `COM11` works directly — the ROM USB-Serial-JTAG is present. |
| Firmware with `ARDUINO_USB_MODE=0` (`msc`, `ramdisk`) | Native port is TinyUSB, `COM11` is gone. Use the escape hatch below, or hold **BOOT**, tap **RESET**, release **BOOT**. |

### Software escape hatch

Because the BOOT button is otherwise the only way back, every firmware here
includes `src/reflash_hatch.h`. It watches UART0 (`COM5`, always present) for the
word `BOOTLOADER` and forces a download-mode reboot in software:

- `ARDUINO_USB_MODE=0` → `usb_persist_restart(RESTART_BOOTLOADER)`, which hands
  the USB PHY back to the USB-Serial-JTAG controller on the way down, so the ROM
  loader reappears on the same cable.
- `ARDUINO_USB_MODE=1` → sets `RTC_CNTL_FORCE_DOWNLOAD_BOOT` and restarts.

Trigger it from the host, then flash normally:

```powershell
"BOOTLOADER" | Out-File -Encoding ascii COM5
```

---

## 6. Build environments

`platformio.ini` now defines four:

| Env | Source | USB mode | Purpose |
|---|---|---|---|
| `msc` | `main.cpp` | TinyUSB | The real firmware: SD card as a USB drive |
| `ramdisk` | `msc_ramdisk.cpp` | TinyUSB | RAM-backed FAT12 volume; proves USB MSC works without the SD card |
| `diag` | `sd_diag.cpp` | Serial/JTAG | Full SD report: geometry, MBR dump, root listing |
| `scan` | `sd_scan.cpp` | Serial/JTAG | Pin health, permutation sweep, broad MISO sweep; re-runs every 10 s |

```bash
pio run -e diag -t upload            # then read COM11
pio run -e msc  -t upload            # then read COM5
```

The `ramdisk` environment is the key isolation tool: if it produces a drive on
the host, USB is proven good and any remaining failure is on the SD side.

---

## 7. Result — working

Firmware `msc` on 3V3 wiring:

```
[SD] pins CS=12 MISO=39 MOSI=14 CLK=40
[SD] initialised at 400 kHz, raising clock
[SD] running at 20000000 Hz
[SD] mounted. type=3 size=31914983424 bytes
[SD] sectorSize=512 sectorCount=62333952
[USB] MSC started
[USB] started
```

Host side:

```
USB\VID_303A&PID_4001\9888E09D1230     USB Mass Storage Device
Disk 2   ESP32 USB_MSC   31914983424   USB
Partition 1   DriveLetter J   FAT32   31913410560
```

End-to-end verification over USB:

| Test | Result |
|---|---|
| Root listing | `System Volume Information/`, `projects/` — matches the ESP32's own read |
| Write 2 MB, read back, SHA256 | **PASS** — `6EA73B45…C562` identical both sides |
| Write throughput | 248 KB/s |
| Read throughput (uncached, `FILE_FLAG_NO_BUFFERING`) | 485 KB/s |

Throughput is limited by SPI-mode SD, as the original README notes. The test
files were removed afterwards; the card holds only its original contents.

## 8. Status

- [x] Chip/flash/PSRAM identified from hardware
- [x] Blocking Cardputer wait removed; M5 dependency dropped
- [x] `ARDUINO_USB_MODE=0` correctly applied over the board definition
- [x] Partition table and flash size corrected for 4 MB
- [x] SD wiring fault located (MISO/MOSI swapped) and corrected
- [x] Flashing path for this board understood and worked around
- [ ] SD card verified reachable on the corrected wiring
- [x] Cold-init bug fixed: identify at 400 kHz, then raise the clock
- [x] End-to-end: card enumerates as drive J:, read and write verified by hash
