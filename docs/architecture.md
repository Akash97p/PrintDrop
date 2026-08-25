# Architecture

PrintDrop is one device wearing two hats: a USB mass storage device for the
printer, and a Wi-Fi file server for everyone else. Both want the same SD card,
and that is the whole design problem.

## Module layout

```
src/printdrop/
  main.cpp        startup, serial console
  config.h        pins, timeouts, AP credentials
  storage.cpp     SD card and USB mass storage arbitration
  net.cpp         Wi-Fi provisioning, mDNS, static IP
  web.cpp         HTTP server and JSON file API
src/diag/         SD and USB diagnostics
src/legacy/       USB mass storage only, no networking
src/common/       shared helpers (reflash hatch)
data/             web UI, flashed to LittleFS
```

## Sharing the card

A USB host caches the FAT. If the ESP32 writes to the card while the printer has
it mounted, the host's cached allocation table goes stale and the next write
from either side corrupts the filesystem. The reverse is also true — the ESP32's
own FATFS cache goes stale if the host writes sectors underneath it.

`storage.cpp` enforces three rules:

**1. One mutex.** Every SD access, from either side, is serialised. The MSC
read/write callbacks and the HTTP handlers contend for the same lock.

**2. Withdraw before writing.** Before the ESP32 modifies the card, the media is
withdrawn from the USB host (`msc.mediaPresent(false)`), and re-presented
afterwards. The host sees a card removal, drops its cache, and re-reads the FAT
— which is what makes an uploaded file appear in the printer's file list without
a reboot.

**3. Remount when the host writes.** MSC write callbacks set a flag. Before the
ESP32 next trusts its own view of the filesystem, it tears down and remounts
FATFS.

Any code path that touches the card goes through `storage::Guard`:

```cpp
storage::Guard g(/*forWrite=*/true);
if (!g.ok()) return sendError(503, "Card busy");
// ... card is exclusively ours, and the host cannot see it ...
```

### Not stalling the printer

MSC callbacks run on the TinyUSB task. They take the mutex with a **2 second
timeout and fail the transfer** rather than block indefinitely:

```cpp
if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(kMscLockTimeoutMs)) != pdTRUE) return -1;
```

This matters: a 20 MB upload holds the write lock for around 80 seconds. Without
the timeout, a printer mid-job would block on a read for the whole upload. With
it, the host sees the media as absent (because the write path withdrew it) and
does not issue reads at all.

## Networking

Credentials live in NVS via `Preferences`, never in the source tree. On boot:

1. No stored SSID, or the stored network cannot be joined within 20 s →
   raise a WPA2 access point (`PrintDrop-Setup`) serving the same UI.
2. Otherwise join as a station, optionally with a static IP.

Either way mDNS is started, so `http://printdrop.local` resolves.

A serial console on UART0 (`help`, `status`, `wifi`, `hostname`, `forget`,
`reboot`) provides headless provisioning without putting secrets in the repo.

## Web layer

A synchronous `WebServer` on port 80. The UI is static files from LittleFS,
pre-compressed copies preferred when present; everything else is a small JSON
API.

| Endpoint | Method | Notes |
|---|---|---|
| `/api/status` | GET | device, card, USB and network state |
| `/api/list` | GET | directory listing |
| `/api/upload` | POST | multipart; target directory in `?path=` |
| `/api/download` | GET | streams a file |
| `/api/delete`, `/api/mkdir`, `/api/rename` | POST | form-encoded |
| `/api/eject` | POST | withdraw and re-present the media |
| `/api/wifi/scan`, `/api/wifi` | GET / POST | provisioning |

Client-supplied paths go through `safePath()`, which rejects anything
containing `..` and normalises separators.

Uploads are driven by repeated callbacks, so the SD lock is acquired on
`UPLOAD_FILE_START` and released on `UPLOAD_FILE_END` or `UPLOAD_FILE_ABORTED`
rather than scoped to one function. An aborted upload deletes its partial file
— a truncated `.gcode` that looks like a valid print job is worse than no file.

Query-string arguments are parsed before the multipart body
(`WebServer::_parseRequest`), which is why `?path=` is readable inside the
upload callback.

## Build environments

| Env | Sources | USB mode | Purpose |
|---|---|---|---|
| `printdrop` | `src/printdrop/` | TinyUSB | The product |
| `msc` | `src/legacy/` | TinyUSB | USB mass storage only, no networking |
| `ramdisk` | `src/diag/msc_ramdisk.cpp` | TinyUSB | RAM-backed FAT12 volume; proves USB MSC without the SD card |
| `diag` | `src/diag/sd_diag.cpp` | Serial/JTAG | SPI speed sweep, geometry, MBR dump, root listing |
| `scan` | `src/diag/sd_scan.cpp` | Serial/JTAG | Pin health, line voltages, pin-permutation sweep |

The diagnostic environments deliberately keep `ARDUINO_USB_MODE=1` so the native
port stays a serial/JTAG device and the board remains trivially flashable while
hardware is being investigated.

`ramdisk` is the key isolation tool: if it produces a drive on the host, the USB
path is proven good and any remaining fault is on the SD side.

## Partitions

4 MB flash, single app image — there is no room for two OTA slots.

| Partition | Offset | Size | Holds |
|---|---|---|---|
| `nvs` | `0x9000` | 20 KB | Wi-Fi credentials, hostname |
| `app0` | `0x10000` | 2688 KB | firmware (~900 KB used) |
| `spiffs` (LittleFS) | `0x2B0000` | 1280 KB | web UI (~62 KB used) |
| `coredump` | `0x3F0000` | 64 KB | crash dumps |

The LittleFS partition is named `spiffs` because that is the label
`LittleFS.begin()` looks for by default, and what PlatformIO's `uploadfs`
targets.

## Measured performance

| Path | Throughput |
|---|---|
| USB read (uncached) | 485 KB/s |
| USB write | 248 KB/s |
| Raw SD SPI | ~910 KB/s |

Bounded by driving the card in SPI mode rather than 4-bit SDIO. A 20 MB print
job takes roughly 80 seconds to upload; the card, not the network, is the
bottleneck.

Verified end to end over USB: a 2 MB write survives a SHA-256 round trip
(`6EA73B45…C562` identical both sides), and the ESP32's own directory listing
matches what the host sees.
