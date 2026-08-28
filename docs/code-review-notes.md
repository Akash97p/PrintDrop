# Code review notes

Seven concerns raised during a full review of the codebase (firmware,
diagnostics, frontend, CI). Ordered by practical impact.

---

## 1. Wi-Fi robustness gap

**Where:** `src/printdrop/net.cpp:198-201`

If the configured AP disappears after boot, `WiFi.setAutoReconnect(true)`
retries forever in station mode and there is **no fallback to the setup AP**.
The device becomes unreachable and un-provisionable until someone physically
reboots it — a real failure mode for a device bolted to a printer.

**Suggestion:** add a watchdog — if the link stays disconnected for N minutes,
drop back to the setup access point (`startAccessPoint()`) so the device is
always reachable.

---

## 2. Auth is Basic auth over plain HTTP

**Where:** `src/printdrop/web.cpp:103-110`, `src/printdrop/auth.cpp:66-100`,
`data/app.js:35-53`

- Credentials travel as base64 (trivially decoded) over unencrypted HTTP.
- The frontend stores the password in `localStorage` in plain text.

Reasonable tradeoff for the platform, but the threat model should be stated
explicitly ("don't use on untrusted networks"), and `sessionStorage` would at
least reduce password persistence on the client.

---

## 3. Upload overwrite destroys the old file before the new one lands

**Where:** `src/printdrop/web.cpp:364`

```cpp
if (SD_FS.exists(uploadName)) SD_FS.remove(uploadName);
```

The old file is deleted before the upload starts. If the upload aborts or fails
mid-way, the previous job is permanently lost (the abort path deletes the
partial file too, `web.cpp:402`).

**Suggestion:** write to a temp file (e.g. `<name>.uploading`) and rename on
success, so the original survives a failed overwrite.

---

## 4. `refreshHostView()` uses a magic delay and releases the lock mid-sequence

**Where:** `src/printdrop/storage.cpp:397-409`

Fixed `delay(600)` between withdraw and re-present. Two problems:

- The delay is host-dependent guesswork, not a real "host has detached" signal.
- The mutex is released during the wait, so a concurrent upload can grab the
  write lock and the re-present can happen while the card is mid-write —
  the host would then read a stale/half-written FAT.

**Suggestion:** poll for the actual host detachment (USB stop/suspend event) or
retry re-presentation until the host is idle, instead of a fixed sleep.

---

## 5. Unpinned toolchain + fragile SDIO core hack

**Where:** `platformio.ini:17`, `src/printdrop/storage.cpp:16-20`

- `platform = espressif32` has no `platform_version`, so the Arduino core can
  float and change behaviour between builds.
- The SDIO path reaches into `SD_MMCFS::_card` through a subclass hack. That
  member is core-version-dependent — an unpinned core can silently break it.

**Suggestion:** pin `platform_version` (and core) in `platformio.ini`.

---

## 6. No firmware CI

**Where:** `.github/workflows/pages.yml` (site only)

The only workflow builds the marketing website. The firmware — six build
environments including the hacky SDIO path — has no CI build, so regressions
are only caught on real hardware by the author.

**Suggestion:** add a workflow that runs `pio run` for the environments (or at
least `printdrop` + `printdrop_spi`) on pushes to feature branches.

---

## 7. Minor nits

| Issue | Where | Note |
|---|---|---|
| SD-OTA version check is string comparison | `src/printdrop/ota.cpp:60` | `"0.10.0"` compares wrong against `"0.9.0"` — use numeric/semver compare |
| File sizes truncated to 32-bit | `src/printdrop/web.cpp:240` | `(uint32_t)f.size()` — files > 4 GB report garbage sizes |
| WS progress JSON escapes only `"` and `\` | `src/printdrop/ws.cpp:37` | Filenames with control chars produce invalid JSON (client tolerates via try/catch) |
| No link-quality readout after boot | `main.cpp` `status` command | RSSI only; marginal wiring is diagnosed only from the boot log |

---

## Priority order

1. Firmware CI build + pin toolchain (#6, #5) — cheap, prevents regressions.
2. AP-fallback watchdog (#1) — biggest real-world robustness win.
3. Temp-file-rename uploads (#3) — protects user data.
4. Document the auth threat model; `sessionStorage` (#2).