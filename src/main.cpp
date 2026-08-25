// USB mass storage firmware: exposes an SPI-attached SD card as a USB disk.
//
// Target: ESP32-S3-DevKitC-1 (no display, no keyboard).
//
// Must be built with ARDUINO_USB_CDC_ON_BOOT=0. USB.h defines
//     ARDUINO_USB_ON_BOOT (ARDUINO_USB_CDC_ON_BOOT|...)
// and the core's app_main() calls USB.begin() when that is set -- before
// setup() ever runs. Once TinyUSB has started, USB.VID()/PID() are silently
// ignored (they return early on _started) and no further interface can be
// added, so the device enumerates as CDC-only with the stock PID and no disk
// ever appears. Leaving CDC off keeps USB.begin() under this file's control.
//
// Logging therefore goes to UART0, i.e. the separate USB-UART bridge (COM5).

#include <Arduino.h>
#include <USB.h>
#include <USBMSC.h>
#include <SPI.h>
#include <SD.h>
#include "reflash_hatch.h"

#ifndef SD_CS_PIN
#define SD_CS_PIN   12
#endif
#ifndef SD_MISO_PIN
#define SD_MISO_PIN 39
#endif
#ifndef SD_MOSI_PIN
#define SD_MOSI_PIN 14
#endif
#ifndef SD_CLK_PIN
#define SD_CLK_PIN  40
#endif

// SD.begin() negotiates down from here; 20 MHz is what an SPI-mode card
// realistically sustains on jumper wiring.
#ifndef SD_SPI_FREQ
#define SD_SPI_FREQ 20000000
#endif

USBMSC msc;
SPIClass sdSPI;

static uint32_t sdSectorSize = 0;
static uint32_t sdSectorCount = 0;

// TinyUSB owns the native port, so UART0 (the USB-UART bridge, COM5) is the
// only console. UART_CONSOLE resolves to it in either USB configuration.
static void logf(const char* fmt, ...) {
    char buf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    UART_CONSOLE.println(buf);
}

static int32_t usbWriteCallback(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    if (sdSectorSize == 0) return -1;
    // The host may hand over a partial sector; refuse rather than corrupt one.
    if (offset != 0 || (bufsize % sdSectorSize) != 0) return -1;

    const uint32_t count = bufsize / sdSectorSize;
    if (lba + count > sdSectorCount) return -1;

    for (uint32_t x = 0; x < count; ++x) {
        if (!SD.writeRAW(buffer + sdSectorSize * x, lba + x)) {
            return -1;
        }
    }
    return bufsize;
}

static int32_t usbReadCallback(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    if (sdSectorSize == 0) return -1;
    if (offset != 0 || (bufsize % sdSectorSize) != 0) return -1;

    const uint32_t count = bufsize / sdSectorSize;
    if (lba + count > sdSectorCount) return -1;

    for (uint32_t x = 0; x < count; ++x) {
        if (!SD.readRAW(reinterpret_cast<uint8_t*>(buffer) + (x * sdSectorSize), lba + x)) {
            return -1;
        }
    }
    return bufsize;
}

static bool usbStartStopCallback(uint8_t power_condition, bool start, bool load_eject) {
    logf("[USB] start/stop: power=%u start=%u eject=%u", power_condition, start, load_eject);
    return true;
}

static void setupUsbEvent() {
    USB.onEvent([](void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
        if (event_base != ARDUINO_USB_EVENTS) return;
        switch (event_id) {
            case ARDUINO_USB_STARTED_EVENT: logf("[USB] started");   break;
            case ARDUINO_USB_STOPPED_EVENT: logf("[USB] stopped");   break;
            case ARDUINO_USB_SUSPEND_EVENT: logf("[USB] suspended"); break;
            case ARDUINO_USB_RESUME_EVENT:  logf("[USB] resumed");   break;
            default: break;
        }
    });
}

// The SD specification requires the card identification sequence (CMD0/CMD8/
// ACMD41) to run at 400 kHz or less; the clock may only be raised once the card
// has left the identification state. SD.begin() runs that whole sequence at
// whatever frequency it is given, so calling it directly at 20 MHz fails on a
// cold card with "Card Failed! cmd: 0x00".
//
// Mount slowly first, then re-mount fast: the card stays initialised until it
// loses power, so the second pass succeeds at full speed.
static bool mountSD() {
    sdSPI.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

    bool initialised = false;
    for (int attempt = 1; attempt <= 5 && !initialised; ++attempt) {
        if (SD.begin(SD_CS_PIN, sdSPI, 400000)) {
            initialised = true;
            break;
        }
        logf("[SD] 400 kHz init attempt %d failed", attempt);
        SD.end();
        delay(500);
    }
    if (!initialised) return false;
    logf("[SD] initialised at 400 kHz, raising clock");

    // Step down through the ladder if the wiring will not carry the top speed.
    const uint32_t ladder[] = {SD_SPI_FREQ, 10000000, 4000000, 1000000};
    for (uint32_t f : ladder) {
        SD.end();
        if (SD.begin(SD_CS_PIN, sdSPI, f)) {
            // Prove the link before trusting it: a bad clock enumerates fine
            // and then hands the host corrupt sectors.
            uint8_t probe[512];
            if (SD.readRAW(probe, 0) && probe[510] == 0x55 && probe[511] == 0xAA) {
                logf("[SD] running at %u Hz", f);
                return true;
            }
            logf("[SD] %u Hz mounted but failed verification, stepping down", f);
        } else {
            logf("[SD] %u Hz mount failed, stepping down", f);
        }
    }

    SD.end();
    if (SD.begin(SD_CS_PIN, sdSPI, 400000)) {
        logf("[SD] falling back to 400 kHz");
        return true;
    }
    return false;
}

void setup() {
    UART_CONSOLE.begin(115200);
    delay(1500);

    logf("");
    logf("=== SD -> USB Mass Storage (ESP32-S3) ===");
    logf("Build: " __DATE__ " " __TIME__);
    logf("[SD] pins CS=%d MISO=%d MOSI=%d CLK=%d",
         SD_CS_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CLK_PIN);

    if (!mountSD()) {
        // Enumerating with no media is worse than not enumerating: the host
        // shows a broken drive. Stop here with a clear message instead.
        logf("[SD] FATAL: no card found.");
        logf("[SD] If the module has an AMS1117 regulator, VCC must be 5V, not 3V3.");
        logf("[SD] Check CS/MISO/MOSI/CLK against the pins printed above.");
        // Keep the escape hatch alive so the board is still reflashable.
        uint32_t last = 0;
        while (true) {
            if (millis() - last > 5000) {
                last = millis();
                logf("[SD] waiting for reset...");
            }
            reflashHatchPoll();
            delay(50);
        }
    }

    sdSectorSize = SD.sectorSize();
    sdSectorCount = SD.numSectors();

    logf("[SD] mounted. type=%u size=%llu bytes", SD.cardType(), SD.cardSize());
    logf("[SD] sectorSize=%u sectorCount=%u", sdSectorSize, sdSectorCount);

    if (sdSectorSize == 0 || sdSectorCount == 0) {
        logf("[SD] FATAL: card reports zero geometry; cannot expose as a disk.");
        while (true) { reflashHatchPoll(); delay(50); }
    }

    setupUsbEvent();

    // These only take effect while TinyUSB is still stopped, which is why this
    // firmware must build with ARDUINO_USB_CDC_ON_BOOT=0 -- see the note above.
    //
    // A PID distinct from the board's stock 303A:1001 stops Windows serving the
    // cached USB-Serial-JTAG descriptor in place of this one.
    USB.VID(0x303A);
    USB.PID(0x4001);
    USB.productName("ESP32-S3 SD Reader");
    USB.manufacturerName("Espressif");
    // Single-function device: let the host classify it from the interface
    // descriptor rather than announcing an IAD composite it is not.
    USB.usbClass(TUSB_CLASS_UNSPECIFIED);
    USB.usbSubClass(0);
    USB.usbProtocol(0);

    msc.vendorID("ESP32");
    msc.productID("USB_MSC");
    msc.productRevision("1.0");
    msc.onRead(usbReadCallback);
    msc.onWrite(usbWriteCallback);
    msc.onStartStop(usbStartStopCallback);
    msc.mediaPresent(true);
    msc.begin(sdSectorCount, sdSectorSize);

    USB.begin();
    logf("[USB] MSC started - the card should appear as a drive on the host.");
}

void loop() {
    reflashHatchPoll();
    delay(50);
}
