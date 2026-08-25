// PrintDrop — a Wi-Fi SD card for 3D printers.
//
// The board plugs into a printer's USB port and appears as an ordinary USB
// flash drive. It also joins the local network and serves a web UI, so print
// jobs can be dropped onto the card from a desk instead of carrying a stick
// across the workshop.
//
// Target: ESP32-S3-DevKitC-1, 4 MB flash, no PSRAM.
//
// Must be built with ARDUINO_USB_CDC_ON_BOOT=0. USB.h derives
// ARDUINO_USB_ON_BOOT from it, and the core's app_main() then calls USB.begin()
// before setup() runs — freezing the USB descriptor before the MSC interface
// can be registered or the PID set. Console output goes to UART0 instead.

#include <Arduino.h>

#include "../common/reflash_hatch.h"
#include "config.h"
#include "net.h"
#include "storage.h"
#include "web.h"

// Serial console on UART0. The web setup portal is the normal route, but a
// headless device on a bench is far easier to provision over the wire — and it
// keeps credentials out of the source tree either way.
static void handleCommand(String line) {
    line.trim();
    if (line.isEmpty()) return;

    if (line == "BOOTLOADER") {
        Serial.println("[hatch] rebooting into download mode...");
        Serial.flush();
        delay(50);
        reflashHatchReboot();
        return;
    }

    String cmd = line, rest;
    const int sp = line.indexOf(' ');
    if (sp > 0) {
        cmd  = line.substring(0, sp);
        rest = line.substring(sp + 1);
        rest.trim();
    }
    cmd.toLowerCase();

    if (cmd == "help") {
        Serial.println("commands:");
        Serial.println("  status                 show device state");
        Serial.println("  wifi <ssid> <password> join a network and reboot");
        Serial.println("  hostname <name>        set the mDNS name and reboot");
        Serial.println("  forget                 clear Wi-Fi settings and reboot");
        Serial.println("  reboot                 restart");
        Serial.println("  BOOTLOADER             reboot into flash download mode");

    } else if (cmd == "status") {
        Serial.printf("mode      : %s\n", net::isAccessPoint() ? "access point" : "station");
        Serial.printf("ssid      : %s\n", net::currentSsid().c_str());
        Serial.printf("hostname  : %s.local\n", net::hostname().c_str());
        Serial.printf("ip        : %s\n", net::localIp().toString().c_str());
        Serial.printf("rssi      : %d dBm\n", (int)net::rssi());
        Serial.printf("card      : %s\n", storage::cardMounted() ? "mounted" : "absent");
        Serial.printf("usb host  : %s\n", storage::usbHostPresent() ? "connected" : "none");
        Serial.printf("usb media : %s\n", storage::usbMediaPresent() ? "presented" : "withdrawn");

    } else if (cmd == "wifi") {
        // The password may contain spaces, so only the first token is the SSID.
        const int sp2 = rest.indexOf(' ');
        if (sp2 <= 0) { Serial.println("usage: wifi <ssid> <password>"); return; }
        net::Config c = net::config();
        c.ssid     = rest.substring(0, sp2);
        c.password = rest.substring(sp2 + 1);
        if (net::saveConfig(c)) { Serial.println("saved, restarting"); delay(300); ESP.restart(); }

    } else if (cmd == "hostname") {
        if (rest.isEmpty()) { Serial.println("usage: hostname <name>"); return; }
        net::Config c = net::config();
        c.hostname = rest;
        if (net::saveConfig(c)) { Serial.println("saved, restarting"); delay(300); ESP.restart(); }

    } else if (cmd == "forget") {
        net::Config c;
        net::saveConfig(c);
        Serial.println("cleared, restarting");
        delay(300);
        ESP.restart();

    } else if (cmd == "reboot") {
        ESP.restart();

    } else {
        Serial.printf("unknown command '%s' — try 'help'\n", cmd.c_str());
    }
}

static void pollConsole() {
    static String line;
    while (Serial.available()) {
        const char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (!line.isEmpty()) { handleCommand(line); line = ""; }
        } else if (line.length() < 160) {
            line += c;
        }
    }
}

static void banner() {
    Serial.println();
    Serial.println("=====================================");
    Serial.println("  " PRINTDROP_NAME " " PRINTDROP_VERSION);
    Serial.println("  Kabani Tech Private Limited");
    Serial.println("=====================================");
    Serial.printf("Build: %s %s\n", __DATE__, __TIME__);
    Serial.printf("SD pins CS=%d MISO=%d MOSI=%d CLK=%d\n",
                  SD_CS_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CLK_PIN);
    Serial.println("Type 'help' for serial commands.");
}

void setup() {
    UART_CONSOLE.begin(115200);
    delay(1200);
    banner();

    if (!storage::begin()) {
        // Without a card there is nothing to serve, but the network side still
        // comes up so the device can be reached and diagnosed.
        Serial.println("[sd] FATAL: no usable card.");
        Serial.println("[sd] If the module has an AMS1117 regulator, VCC must be 3V3.");
        Serial.println("[sd] Check CS/MISO/MOSI/CLK against the pins above.");
    }

    const bool joined = net::begin();
    web::begin();

    if (joined) {
        Serial.printf("[ready] http://%s.local  or  http://%s\n",
                      net::hostname().c_str(), net::localIp().toString().c_str());
    } else {
        Serial.printf("[ready] setup portal at http://%s (join \"%s\")\n",
                      net::localIp().toString().c_str(), AP_SSID_PREFIX);
    }
}

void loop() {
    web::loop();
    net::loop();
    // Owns the UART instead of reflashHatchPoll(), so BOOTLOADER and the
    // provisioning commands do not fight over the same input stream.
    pollConsole();

    if (web::rebootRequested()) {
        Serial.println("[sys] settings saved, restarting");
        delay(400);
        ESP.restart();
    }
}
