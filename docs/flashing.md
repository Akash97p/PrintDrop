# Flashing this board

Two independent things make this harder than a normal ESP32 board. Both have
workarounds; neither is obvious from the error messages.

## The auto-reset circuit is only half wired

`esptool` on `COM5` always fails with:

```
A fatal error occurred: Failed to connect to Espressif device:
Wrong boot mode detected (0x8)! The chip needs to be in download mode.
```

Driving the reset lines by hand shows why:

```
DTR/RTS toggled -> ESP-ROM:esp32s3-20210327
                   rst:0x1 (POWERON), boot:0x8 (SPI_FAST_FLASH_BOOT)
```

The chip **does** reset — so RTS → EN is connected — but comes up in normal
flash boot, meaning **DTR → GPIO0 is not connected**. `COM5` can therefore never
put this board into download mode on its own, whatever esptool options are used.
This is a board wiring limitation, not a software problem.

### The consequence: RTS resets the chip

Because RTS drives EN, **opening or closing the UART port resets the chip**.
Anything that touches `COM5` between putting the board in download mode and
flashing it will knock it straight back out. Tools must hold RTS and DTR
deasserted:

```python
s = serial.Serial()
s.port = "COM5"; s.baudrate = 115200
s.rts = False; s.dtr = False     # set before opening
s.open()
```

This cost real time to find — the escape hatch below appeared to work, yet the
board was never in download mode by the time esptool connected.

## The native port disappears

Firmware that claims the native USB port for TinyUSB replaces the
USB-Serial-JTAG device, so `COM11` vanishes and `esptool` cannot reach the board
there either.

| Firmware | How to flash |
|---|---|
| `ARDUINO_USB_MODE=1` (`diag`, `scan`) | `COM11` directly — the ROM USB-Serial-JTAG is present |
| `ARDUINO_USB_MODE=0` (`printdrop`, `msc`, `ramdisk`) | escape hatch below, or the BOOT button |

## Software escape hatch

Every firmware watches UART0 for the word `BOOTLOADER` and forces a
download-mode reboot:

```powershell
"BOOTLOADER" | Out-File -Encoding ascii COM5
```

Implemented in `src/common/reflash_hatch.h`:

- `ARDUINO_USB_MODE=0` → `usb_persist_restart(RESTART_BOOTLOADER)`, which hands
  the USB PHY back to the USB-Serial-JTAG controller on the way down, so the ROM
  loader reappears on the same cable.
- `ARDUINO_USB_MODE=1` → sets `RTC_CNTL_FORCE_DOWNLOAD_BOOT` and restarts.

`printdrop` folds this into its serial console, so `BOOTLOADER` and the
provisioning commands share one input stream.

Confirm it worked before flashing — the ROM announces itself:

```
rst:0xc (RTC_SW_CPU_RST),boot:0x0 (DOWNLOAD(USB/UART0))
waiting for download
```

## Manual fallback

Hold **BOOT**, tap **RESET**, release **BOOT**. Always works, needs a hand on
the board.

## Flashing firmware and filesystem together

`uploadfs` and `upload` each end with a hard reset, which boots the application
and takes the download port away — so running them back to back fails on the
second. Write both images in one esptool invocation instead:

```bash
esptool.py --chip esp32s3 --port COM11 --before default_reset --after hard_reset \
  write_flash -z \
  0x0      .pio/build/printdrop/bootloader.bin \
  0x8000   .pio/build/printdrop/partitions.bin \
  0x10000  .pio/build/printdrop/firmware.bin \
  0x2B0000 .pio/build/printdrop/littlefs.bin
```

The `0x2B0000` offset is the LittleFS partition; see
[architecture.md](architecture.md#partitions).

## Windows may serve a stale USB descriptor

Windows caches configuration descriptors per VID/PID. Because the board's stock
ID (`303A:1001`) is the same as the ROM's USB-Serial-JTAG device, a firmware
reusing it can enumerate with the *old* interface layout and refuse to open.

Each firmware therefore claims its own PID (`303A:4001`–`4003`). If ports still
behave strangely after a mode change — appearing, refusing to open, or reporting
*"a device attached to the system is not functioning"* — unplug and replug the
native USB cable to force a clean re-enumeration.
