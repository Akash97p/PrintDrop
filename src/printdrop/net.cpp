#include "net.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
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
#if PRINTDROP_ENABLE_MDNS
    if (!MDNS.begin(host.c_str())) {
        log("[net] mDNS failed to start");
    } else {
        MDNS.addService("http", "tcp", 80);
        MDNS.addServiceTxt("http", "tcp", "path", "/");
        log("[net] mDNS: http://%s.local", host.c_str());
    }
#else
    (void)host;
#endif
}

#if PRINTDROP_ENABLE_LLMNR
WiFiUDP llmnrUdp;
bool    llmnrRunning = false;

void startLlmnr(const String& host) {
    // LLMNR answers single-label names (http://printdrop) on Windows without
    // Bonjour. Minimal UDP responder on 5355, unicast answer.
    if (llmnrRunning) { llmnrUdp.stop(); llmnrRunning = false; }
    if (host.isEmpty()) return;
    if (!llmnrUdp.beginMulticast(IPAddress(224,0,0,252), 5355)) {
        // Fallback unicast
        if (!llmnrUdp.begin(5355)) {
            log("[net] LLMNR: failed to bind 5355");
            return;
        }
    }
    llmnrRunning = true;
    log("[net] LLMNR: http://%s (single-label)", host.c_str());
}

void pollLlmnr(const String& host) {
    if (!llmnrRunning) return;
    int sz = llmnrUdp.parsePacket();
    if (sz <= 0) return;
    uint8_t buf[512];
    int len = llmnrUdp.read(buf, sizeof(buf));
    if (len < 12) return;
    // DNS header: ID 2, flags 2, QDCOUNT 2, AN 2, NS 2, AR 2
    uint16_t qd = (buf[4] << 8) | buf[5];
    if (qd == 0) return;
    // Parse QNAME
    int pos = 12;
    String qname;
    while (pos < len && buf[pos] != 0) {
        uint8_t l = buf[pos++];
        if (pos + l > len) return;
        if (!qname.isEmpty()) qname += ".";
        for (uint8_t i=0;i<l;++i) qname += (char)tolower(buf[pos+i]);
        pos += l;
    }
    if (pos >= len) return;
    pos++; // null
    if (pos + 4 > len) return;
    uint16_t qtype = (buf[pos]<<8)|buf[pos+1];
    uint16_t qclass = (buf[pos+2]<<8)|buf[pos+3];
    // Only A / ANY
    if (qtype != 1 && qtype != 255) return;
    if (qclass != 1 && qclass != 255) return;
    String hostL = host; hostL.toLowerCase();
    String hostLocal = hostL + ".local";
    qname.toLowerCase();
    if (qname != hostL && qname != hostLocal) return;
    // Build response: copy header, set QR=1, AA=1, copy question, add answer
    uint8_t resp[512];
    memcpy(resp, buf, len);
    resp[2] |= 0x80; // QR
    resp[2] |= 0x04; // AA
    resp[3] &= ~0x0F; // RCODE 0
    // ANCOUNT = 1
    resp[6]=0; resp[7]=1;
    int rpos = len;
    // Answer: pointer to QNAME (0xC0 0x0C), type A, class IN, TTL 30, RDLEN 4, RDATA IP
    if (rpos + 16 > (int)sizeof(resp)) return;
    resp[rpos++]=0xC0; resp[rpos++]=0x0C;
    resp[rpos++]=0x00; resp[rpos++]=0x01; // A
    resp[rpos++]=0x00; resp[rpos++]=0x01; // IN
    resp[rpos++]=0x00; resp[rpos++]=0x00; resp[rpos++]=0x00; resp[rpos++]=0x1E; // 30s
    resp[rpos++]=0x00; resp[rpos++]=0x04;
    IPAddress ip = WiFi.localIP();
    if (ip == IPAddress(0,0,0,0)) ip = WiFi.softAPIP();
    resp[rpos++]=ip[0]; resp[rpos++]=ip[1]; resp[rpos++]=ip[2]; resp[rpos++]=ip[3];
    llmnrUdp.beginPacket(llmnrUdp.remoteIP(), llmnrUdp.remotePort());
    llmnrUdp.write(resp, rpos);
    llmnrUdp.endPacket();
}
#else
void startLlmnr(const String&) {}
void pollLlmnr(const String&) {}
#endif

}  // namespace

bool begin() {
    active = load();

    WiFi.persistent(false);
    WiFi.setHostname(active.hostname.c_str());

    if (active.ssid.isEmpty()) {
        log("[net] no network configured");
        startAccessPoint();
        startMdns(active.hostname);
        startLlmnr(active.hostname);
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
        startLlmnr(active.hostname);
        return false;
    }

    apMode = false;
    // Reconnect automatically if the office AP drops.
    WiFi.setAutoReconnect(true);
    log("[net] joined \"%s\" as %s (%d dBm)",
        active.ssid.c_str(), WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
    startMdns(active.hostname);
    startLlmnr(active.hostname);
    log("[net] also at http://%s/ and http://%s.local/ (mDNS+LLMNR)", active.hostname.c_str(), active.hostname.c_str());
    return true;
}

void loop() {
    pollLlmnr(active.hostname);
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
