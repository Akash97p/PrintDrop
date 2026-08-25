#include "net.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>

#include "config.h"

namespace net {
namespace {

Preferences prefs;
Config      active;
bool        apMode = false;

const char* kNamespace = "printdrop";

void log(const char* fmt, ...) {
    char buf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.println(buf);
}

Config load() {
    Config c;
    // Opened read-write: a read-only open of a namespace that does not exist
    // yet logs an nvs_open NOT_FOUND error on every first boot. Opening for
    // write creates it silently; nothing is written unless saveConfig runs.
    prefs.begin(kNamespace, false);
    c.ssid      = prefs.getString("ssid", "");
    c.password  = prefs.getString("pass", "");
    c.hostname  = prefs.getString("host", DEFAULT_HOSTNAME);
    c.useStatic = prefs.getBool("static", false);
    c.ip        = prefs.getString("ip", "");
    c.gateway   = prefs.getString("gw", "");
    c.mask      = prefs.getString("mask", "255.255.255.0");
    c.dns       = prefs.getString("dns", "");
    prefs.end();
    if (c.hostname.isEmpty()) c.hostname = DEFAULT_HOSTNAME;
    return c;
}

void startAccessPoint() {
    apMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID_PREFIX, AP_PASSWORD);
    log("[net] access point \"%s\" up at %s",
        AP_SSID_PREFIX, WiFi.softAPIP().toString().c_str());
    log("[net] password: %s", AP_PASSWORD);
}

bool applyStaticIp(const Config& c) {
    IPAddress ip, gw, mask, dns;
    if (!ip.fromString(c.ip) || !gw.fromString(c.gateway) || !mask.fromString(c.mask)) {
        log("[net] static IP settings are malformed, using DHCP");
        return false;
    }
    if (!c.dns.isEmpty() && dns.fromString(c.dns)) {
        return WiFi.config(ip, gw, mask, dns);
    }
    return WiFi.config(ip, gw, mask);
}

void startMdns(const String& host) {
    if (!MDNS.begin(host.c_str())) {
        log("[net] mDNS failed to start");
        return;
    }
    MDNS.addService("http", "tcp", 80);
    log("[net] discoverable as http://%s.local", host.c_str());
}

}  // namespace

bool begin() {
    active = load();

    WiFi.persistent(false);
    WiFi.setHostname(active.hostname.c_str());

    if (active.ssid.isEmpty()) {
        log("[net] no network configured");
        startAccessPoint();
        startMdns(active.hostname);
        return false;
    }

    WiFi.mode(WIFI_STA);
    if (active.useStatic) applyStaticIp(active);
    WiFi.setHostname(active.hostname.c_str());
    WiFi.begin(active.ssid.c_str(), active.password.c_str());
    log("[net] joining \"%s\"...", active.ssid.c_str());

    const uint32_t deadline = millis() + WIFI_CONNECT_TIMEOUT_MS;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
        delay(250);
    }

    if (WiFi.status() != WL_CONNECTED) {
        log("[net] could not join \"%s\"", active.ssid.c_str());
        WiFi.disconnect(true);
        startAccessPoint();
        startMdns(active.hostname);
        return false;
    }

    apMode = false;
    // Reconnect automatically if the office AP drops.
    WiFi.setAutoReconnect(true);
    log("[net] joined \"%s\" as %s (%d dBm)",
        active.ssid.c_str(), WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
    startMdns(active.hostname);
    return true;
}

void loop() {
    // ESPmDNS on this core needs no periodic servicing; kept as the hook for
    // any future connection babysitting.
}

bool      isAccessPoint() { return apMode; }
String    currentSsid()   { return apMode ? String(AP_SSID_PREFIX) : active.ssid; }
String    hostname()      { return active.hostname; }
IPAddress localIp()       { return apMode ? WiFi.softAPIP() : WiFi.localIP(); }
int32_t   rssi()          { return apMode ? 0 : WiFi.RSSI(); }
Config    config()        { return active; }

bool saveConfig(const Config& cfg) {
    if (!prefs.begin(kNamespace, false)) return false;
    prefs.putString("ssid", cfg.ssid);
    prefs.putString("pass", cfg.password);
    prefs.putString("host", cfg.hostname.isEmpty() ? String(DEFAULT_HOSTNAME) : cfg.hostname);
    prefs.putBool("static", cfg.useStatic);
    prefs.putString("ip",   cfg.ip);
    prefs.putString("gw",   cfg.gateway);
    prefs.putString("mask", cfg.mask.isEmpty() ? String("255.255.255.0") : cfg.mask);
    prefs.putString("dns",  cfg.dns);
    prefs.end();
    log("[net] configuration saved for \"%s\"", cfg.ssid.c_str());
    return true;
}

}  // namespace net
