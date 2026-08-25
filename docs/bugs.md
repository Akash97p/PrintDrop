# Bugs found, and why the code looks like this

A record of every fault hit while porting from an M5Stack Cardputer to a bare
ESP32-S3-DevKitC-1. Most produce **no error message at all** — the device simply
does nothing — so each is written up with the evidence that identified it.

- [Firmware: no USB device appeared](#firmware-no-usb-device-appeared)
- [USB: descriptor frozen before `setup()`](#usb-descriptor-frozen-before-setup)
- [SD: the card was invisible](#sd-the-card-was-invisible)
- [SD: cards must be identified at 400 kHz](#sd-cards-must-be-identified-at-400-khz)

---

## Firmware: no USB device appeared

Four independent defects. Any one alone is enough to produce the reported
symptom — board resets, nothing enumerates.

### `setup()` never finished

`main.cpp` called `displayWelcome()` before touching USB, and that function ends
in:

```cpp
while (true) {
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange()) break;   // never true
    delay(5);
}
```

A DevKitC-1 has no Cardputer keyboard, so `isChange()` never returns true. The
loop spins forever, `USB.begin()` is never reached, and the native port stays in
its ROM USB-Serial-JTAG role — exactly what the host saw: `COM11` enumerating as
*"USB JTAG/serial debug unit"* rather than as a disk.

`M5Cardputer.begin()` also drives a display and keyboard matrix that do not
exist on this board.

**Fix:** dropped M5Unified/M5Cardputer entirely; the firmware is headless and
reports over serial.

### `ARDUINO_USB_MODE=1` compiles USBMSC out

The PlatformIO board definition for `esp32-s3-devkitc-1` hard-codes:

```json
"extra_flags": [ "-DARDUINO_ESP32S3_DEV", "-DARDUINO_USB_MODE=1", ... ]
```

`ARDUINO_USB_MODE=1` means *"use the hardware USB-Serial-JTAG peripheral"*. In
that mode `USBMSC.h` is an empty shell — its whole body sits behind
`#if CONFIG_TINYUSB_MSC_ENABLED` — and the TinyUSB stack that would serve it is
never attached to the USB PHY. **USB mass storage cannot work at
`ARDUINO_USB_MODE=1`.**

The commented-out line in the upstream `platformio.ini` would not have helped;
it set the same wrong value:

```ini
; -D ARDUINO_USB_MODE=1
```

**Fix:** `ARDUINO_USB_MODE=0`, applied through `board_build.extra_flags`.
Appending `-D ARDUINO_USB_MODE=0` to `build_flags` is *not* enough — the board's
flag is emitted first and the result is a macro redefinition with undefined
precedence.

### Partition table sized for 8 MB flash on a 4 MB chip

The stock board definition uses `default_8MB.csv`, and the upstream
`partitions.csv` (never actually referenced from `platformio.ini`) ran to
`0x800000`. Both place partitions past the end of a 4 MB chip. `platformio.ini`
also declared `board_upload.flash_size = 4MB` while leaving the 8 MB table in
place — an inconsistent pair.

**Fix:** an explicit 4 MB table, plus matching `board_upload.flash_size` and
`maximum_size`. See [architecture.md](architecture.md#partitions).

### PSRAM configured on a board that has none

```ini
board_build.arduino.memory_type = qio_qspi
board_build.psram = qspi
```

There is no PSRAM on this chip. The `psram` setting was removed; `qio_qspi` is
the correct SDK variant for quad flash with no PSRAM and was kept.

---

## USB: descriptor frozen before `setup()`

With the build flags fixed, the RAM-disk self-test enumerated — but as **CDC
only**, with no mass-storage interface:

```
USB\VID_303A&PID_1001\…              USB Composite Device
USB\VID_303A&PID_1001&MI_00\…   "USB JTAG/serial debug unit (Interface 0)"
USB\VID_303A&PID_1001&MI_01\…   "USB Serial Device (COM27)"
```

Only `MI_00`/`MI_01`, the two CDC interfaces. No `MI_02` for MSC. Setting a
distinct PID with `USB.PID()` also had no effect. Both failures share one cause.

### Root cause

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

**`ARDUINO_USB_CDC_ON_BOOT=1` implies `USB.begin()` at boot.** That sets
`ESPUSB::_started`, after which every configuration setter is a silent no-op:

```c
bool ESPUSB::PID(uint16_t p){
    if(!_started){ pid = p; }
    return !_started;          // returns false, nobody checks
}
```

and `tinyusb_enable_interface()` refuses outright:

```c
log_e("TinyUSB has already started! Interface %s not enabled", ...);
```

The descriptor was frozen — CDC only, stock PID — before a single line of
`setup()` ran. **Enabling a USB debug console is what stopped the disk
appearing.**

The stock PID is `0x1001` because PlatformIO derives `USB_VID`/`USB_PID` from
the board definition's `hwids`, which for `esp32-s3-devkitc-1` is
`[["0x303A","0x1001"]]` — the same ID as the ROM's USB-Serial-JTAG device.
Windows had cached that descriptor and kept serving the stale layout when the
same VID/PID reappeared with a different configuration.

**Fix:** build TinyUSB environments with `ARDUINO_USB_CDC_ON_BOOT=0`, so
`setup()` owns the whole sequence: register MSC → set VID/PID/class →
`msc.begin()` → `USB.begin()`. Console output moves to UART0, which is always
present anyway.

| Firmware | VID:PID | Device class |
|---|---|---|
| `printdrop` | `303A:4003` | `TUSB_CLASS_UNSPECIFIED` |
| `msc` | `303A:4001` | `TUSB_CLASS_UNSPECIFIED` |
| `ramdisk` | `303A:4002` | `TUSB_CLASS_UNSPECIFIED` |

`TUSB_CLASS_UNSPECIFIED` replaces the core's default IAD composite class: with a
single interface there is no composite to announce, and the host classifies from
the interface descriptor.

---

## SD: the card was invisible

Diagnostic firmware reported the card completely silent on the documented pins:

```
MISO with pull-up   : 1
MISO with pull-down : 0        -> floating, nothing driving it
CMD0 (GO_IDLE_STATE) response: 0x00     (a real card answers 0x01)
SD.begin() failed at 400 kHz, 1, 4, 10, 20 and 25 MHz
```

A second firmware bit-bangs the SD init sequence so it can drive arbitrary
GPIOs, and tests each pin's health:

```
GPIO12 (CS  ): drive HIGH -> 1, drive LOW -> 0   OK
GPIO39 (MISO): drive HIGH -> 1, drive LOW -> 0   OK
GPIO14 (MOSI): drive HIGH -> 0, drive LOW -> 0   *** FAULT ***
GPIO40 (CLK ): drive HIGH -> 1, drive LOW -> 0   OK
```

**GPIO14 could not be driven high** — the signature of another driver holding
the line. The SD card's own DO pin was on GPIO14, winning against the ESP32's
output.

**Cause: MISO and MOSI were swapped.** The card was never at fault and was never
reachable to be tested — with MISO/MOSI crossed, no card of any kind could have
responded.

### The fault followed the wire

After rewiring, the hard-low condition **moved to GPIO39 along with the MISO
wire**:

| Pin | Before rewire | After rewire |
|---|---|---|
| GPIO14 | **stuck LOW** (was MISO) | OK (now MOSI) |
| GPIO39 | OK (was MOSI) | **driven LOW externally** (now MISO) |

Confirming the swap was real and corrected, while something on the MISO line was
still holding it low.

### The 5 V detour

The module was moved to 5 V and the card immediately responded. It was also the
wrong fix: on 5 V the module drives the SPI lines above 3.3 V, outside the
ESP32-S3's absolute maximum. Returning to 3V3 kept the card working *and* put
the lines back in range, so 3V3 is correct.

An earlier explanation blaming AMS1117 dropout was wrong — the module
demonstrably works on 3.3 V. The original failure was most likely a marginal
connection reseated during the rewiring. Details and measurements are in
[hardware.md](hardware.md#power-use-3v3).

Neither the card nor the ESP32 was damaged.

### What this added to the diagnostics

The `scan` environment now includes a line-voltage test that separates the three
cases a digital read cannot: ~0 V is a hard short to ground, ~0.3–0.9 V is an
ESD diode clamp (the module has no VCC), ~3.3 V is free, and a pegged ADC means
the line is being driven out of spec.

---

## SD: cards must be identified at 400 kHz

The SPI speed sweep reported every clock up to 25 MHz as clean, so the firmware
was written to call `SD.begin(cs, spi, 20000000)` directly. On a cold card that
fails:

```
sdCommand(): Card Failed! cmd: 0x00
```

**The sweep was measuring the wrong thing.** Its 400 kHz pass identified the
card, and the card stayed initialised for the rest of the sweep — so every later
pass only re-mounted an already-awake card. It validated sustained data
integrity at speed, not cold initialisation.

The SD specification requires the identification sequence (CMD0/CMD8/ACMD41) to
run at **400 kHz or below**; the clock may only rise afterwards. `SD.begin()`
runs that whole sequence at whatever frequency it is handed.

**Fix:** `mountSD()` identifies at 400 kHz, then re-mounts at full speed,
stepping down a ladder (20 → 10 → 4 → 1 MHz) and verifying the MBR signature at
each rung before trusting the link — a marginal clock mounts fine and then
serves corrupt sectors.
