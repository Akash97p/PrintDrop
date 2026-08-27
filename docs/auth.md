# Authentication

PrintDrop's web UI and JSON API can be protected with HTTP Basic auth. Credentials live in NVS (`printdrop` namespace `auth_user` / `auth_hash` as SHA-256 hex), never in the source tree.

## Defaults

Build flags in `platformio.ini:150` seed a fresh flash:

```ini
-D PRINTDROP_AUTH_USER=\"admin\"
-D PRINTDROP_AUTH_PASS_HASH=\"63402a9a36ef9d75badb66d958ad1decec6c9af4b8757ae77d3189ab0d6f3d68\"
; echo -n "printdrop" | sha256sum -> above
-D PRINTDROP_AUTH_REQUIRED=1  ; 0 = open
```

All values are `#ifndef`-guarded in `src/printdrop/config.h:100` so per-device overrides work. To change at build time:

```bash
pio run -e printdrop --build-flags "-D PRINTDROP_AUTH_USER=\"shop\" -D PRINTDROP_AUTH_PASS_HASH=\"$(echo -n newPass | sha256sum | cut -d' ' -f1)\""
```

## Setting at runtime

**Serial** (`COM5` 115200):
```
auth <user> <pass>      # set and reboot
passwd <newPass>        # keep user, change pass
clear-auth              # reset to build-flag defaults
status                  # shows auth user / on|off
```

**Web UI** `Settings → Web UI Login`: user + new pass + confirm → `POST /api/auth/set` (requires current auth).

**Button** long-press 5 s → factory reset clears Wi-Fi *and* auth (`src/printdrop/button.cpp:44`, `main.cpp:140`).

**API** `GET /api/auth/status` is public; all mutations (`/api/list`, `/api/delete`, `/api/upload`, `/api/wifi`, `/api/ota`…) send `WWW-Authenticate: Basic` on 401 and check `Authorization: Basic <b64>` via `src/printdrop/auth.cpp:44`.

## Disabling

For a trusted LAN: `-D PRINTDROP_AUTH_REQUIRED=0` or via NVS key `auth_required`. The header is still accepted but not required. Not recommended on shared networks.
