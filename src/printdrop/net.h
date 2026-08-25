#pragma once

// Wi-Fi provisioning and discovery.
//
// Credentials live in NVS, never in the source tree. With none stored, or when
// the stored network cannot be joined, the device raises its own access point
// so the setup page is always reachable.

#include <Arduino.h>
#include <IPAddress.h>

namespace net {

struct Config {
    String ssid;
    String password;
    String hostname   = "printdrop";
    bool   useStatic  = false;
    String ip, gateway, mask, dns;
};

bool begin();                 // returns true if joined a network, false if in AP mode
void loop();

bool   isAccessPoint();
String currentSsid();
String hostname();
IPAddress localIp();
int32_t rssi();

Config config();
// Persists to NVS. The caller is expected to reboot; the radio has to restart
// either way and a clean boot avoids half-applied static-IP state.
bool saveConfig(const Config& cfg);

}  // namespace net
