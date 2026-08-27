# Discovery — mDNS + LLMNR

PrintDrop advertises on all active interfaces so `http://printdrop.local` works out of the box and `http://printdrop` works on Windows without Bonjour.

## What runs

* **mDNS** (`src/printdrop/net.cpp:67`): `MDNS.begin(host)` + `_http._tcp` on port 80. Answers `hostname.local`. Enabled via `PRINTDROP_ENABLE_MDNS` (`config.h:123`).
* **LLMNR** (`net.cpp:74`): minimal UDP 5355 responder ( unicast, `224.0.0.252`). Answers single-label `hostname` and `hostname.local` A queries. Enabled via `PRINTDROP_ENABLE_LLMNR`. Windows LLMNR and Linux `systemd-resolved` both resolve the bare name.
* **Static IP / DNS rewrite** (`README.md:Finding it on the network`) remains an option for fixed addresses.

Both use the same `hostname` from NVS (`DEFAULT_HOSTNAME "printdrop"` `config.h:78`), settable via `Settings → Hostname`, serial `hostname <name>`, or `platformio.ini` build flags. `/api/status` reports `discovery {mdns, llmnr}` and `hostname`.

## Verifying

```bash
# mDNS
avahi-resolve -n printdrop.local   # Linux
dns-sd -G v4 printdrop.local       # macOS
ping printdrop.local

# LLMNR (Windows, or `systemd-resolved` Linux)
ping printdrop
nslookup printdrop
```

If both fail, use the IP from serial `status` or `http://192.168.4.1` in AP mode.
