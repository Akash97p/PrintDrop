#include "storage.h"

#include <USB.h>
#include <USBMSC.h>
#include <esp32-hal-tinyusb.h>   // TUSB_CLASS_* — not pulled in by USB.h
#include <SPI.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config.h"

namespace storage {
namespace {

USBMSC      msc;
SPIClass    sdSPI;
SemaphoreHandle_t sdMutex = nullptr;

bool     mounted        = false;
uint32_t secSize        = 0;
uint32_t secCount       = 0;
uint32_t activeFreq     = 0;

volatile bool hostWrote      = false;   // host changed sectors under our FATFS
volatile bool hostAttached   = false;
volatile bool mediaOffered   = false;

// How many nested write-locks have withdrawn the media, so the outermost one
// restores it.
int  mediaWithdrawnDepth = 0;

void log(const char* fmt, ...) {
    char buf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.println(buf);
}

// --- SD mounting -----------------------------------------------------------

// The SD specification requires identification at <=400 kHz; only afterwards
// may the clock rise. SD.begin() runs that sequence at whatever frequency it is
// handed, so a cold card must be mounted slowly first.
bool mountCard() {
    sdSPI.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

    bool identified = false;
    for (int attempt = 1; attempt <= 5 && !identified; ++attempt) {
        if (SD.begin(SD_CS_PIN, sdSPI, 400000)) {
            identified = true;
            break;
        }
        SD.end();
        delay(400);
    }
    if (!identified) {
        log("[sd] identification failed at 400 kHz");
        return false;
    }

    const uint32_t ladder[] = {SD_SPI_FREQ, 10000000, 4000000, 1000000};
    for (uint32_t f : ladder) {
        SD.end();
        if (!SD.begin(SD_CS_PIN, sdSPI, f)) continue;
        // Verify before trusting: a marginal clock mounts fine and then serves
        // corrupt sectors.
        uint8_t probe[512];
        if (SD.readRAW(probe, 0) && probe[510] == 0x55 && probe[511] == 0xAA) {
            activeFreq = f;
            secSize    = SD.sectorSize();
            secCount   = SD.numSectors();
            log("[sd] mounted at %u Hz, %u sectors x %u bytes", f, secCount, secSize);
            return true;
        }
    }

    SD.end();
    if (SD.begin(SD_CS_PIN, sdSPI, 400000)) {
        activeFreq = 400000;
        secSize    = SD.sectorSize();
        secCount   = SD.numSectors();
        log("[sd] fell back to 400 kHz");
        return true;
    }
    return false;
}

// Re-read the filesystem if the USB host has changed it underneath us.
void refreshMountIfStale() {
    if (!hostWrote) return;
    hostWrote = false;
    log("[sd] host wrote sectors, remounting FATFS");
    SD.end();
    sdSPI.end();
    mounted = mountCard();
}

// --- USB MSC callbacks -----------------------------------------------------
// These run on the TinyUSB task. They must never block for long, so they take
// the mutex with a short timeout and fail the transfer rather than stall USB.

const uint32_t kMscLockTimeoutMs = 2000;

int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    if (!mounted || secSize == 0) return -1;
    if (offset != 0 || (bufsize % secSize) != 0) return -1;
    const uint32_t count = bufsize / secSize;
    if (lba + count > secCount) return -1;

    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(kMscLockTimeoutMs)) != pdTRUE) return -1;
    int32_t result = bufsize;
    for (uint32_t i = 0; i < count; ++i) {
        if (!SD.readRAW(reinterpret_cast<uint8_t*>(buffer) + i * secSize, lba + i)) {
            result = -1;
            break;
        }
    }
    xSemaphoreGive(sdMutex);
    return result;
}

int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    if (!mounted || secSize == 0) return -1;
    if (offset != 0 || (bufsize % secSize) != 0) return -1;
    const uint32_t count = bufsize / secSize;
    if (lba + count > secCount) return -1;

    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(kMscLockTimeoutMs)) != pdTRUE) return -1;
    int32_t result = bufsize;
    for (uint32_t i = 0; i < count; ++i) {
        if (!SD.writeRAW(buffer + i * secSize, lba + i)) {
            result = -1;
            break;
        }
    }
    // Our cached FATFS view is now suspect regardless of success.
    hostWrote = true;
    xSemaphoreGive(sdMutex);
    return result;
}

bool onStartStop(uint8_t power_condition, bool start, bool load_eject) {
    if (load_eject && !start) {
        log("[usb] host ejected the media");
    }
    return true;
}

void onUsbEvent(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (base != ARDUINO_USB_EVENTS) return;
    switch (id) {
        case ARDUINO_USB_STARTED_EVENT: hostAttached = true;  log("[usb] host attached"); break;
        case ARDUINO_USB_STOPPED_EVENT: hostAttached = false; log("[usb] host gone");     break;
        case ARDUINO_USB_SUSPEND_EVENT: hostAttached = false; log("[usb] suspended");     break;
        case ARDUINO_USB_RESUME_EVENT:  hostAttached = true;  log("[usb] resumed");       break;
        default: break;
    }
}

void setMedia(bool present) {
    if (mediaOffered == present) return;
    mediaOffered = present;
    msc.mediaPresent(present);
}

}  // namespace

// --- Public API ------------------------------------------------------------

bool begin() {
    sdMutex = xSemaphoreCreateMutex();
    if (!sdMutex) return false;

    mounted = mountCard();
    if (!mounted) return false;
    if (secSize == 0 || secCount == 0) {
        log("[sd] card reports zero geometry");
        return false;
    }

    USB.onEvent(onUsbEvent);

    USB.VID(0x303A);
    USB.PID(0x4003);
    USB.productName(PRINTDROP_NAME);
    USB.manufacturerName("Kabani Tech");
    // Single-function device: let the host classify from the interface
    // descriptor rather than announcing an IAD composite it is not.
    USB.usbClass(TUSB_CLASS_UNSPECIFIED);
    USB.usbSubClass(0);
    USB.usbProtocol(0);

    msc.vendorID("PrntDrop");
    msc.productID("SD Card");
    msc.productRevision("1.0");
    msc.onRead(onRead);
    msc.onWrite(onWrite);
    msc.onStartStop(onStartStop);
    msc.mediaPresent(true);
    mediaOffered = true;
    msc.begin(secCount, secSize);

    USB.begin();
    log("[usb] mass storage started");
    return true;
}

bool     cardMounted()   { return mounted; }
uint32_t sectorCount()   { return secCount; }
uint32_t sectorSize()    { return secSize; }
uint32_t spiFrequency()  { return activeFreq; }
bool     usbHostPresent() { return hostAttached; }
bool     usbMediaPresent(){ return mediaOffered; }

uint64_t totalBytes() {
    Guard g(false, 3000);
    return g.ok() ? SD.totalBytes() : 0;
}
uint64_t usedBytes() {
    Guard g(false, 3000);
    return g.ok() ? SD.usedBytes() : 0;
}
uint64_t freeBytes() {
    Guard g(false, 3000);
    if (!g.ok()) return 0;
    const uint64_t total = SD.totalBytes();
    const uint64_t used  = SD.usedBytes();
    return total > used ? total - used : 0;
}

bool lock(bool forWrite, uint32_t timeoutMs) {
    if (!sdMutex) return false;
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) return false;

    if (forWrite) {
        // Withdraw the card so the host stops reading and drops its FAT cache.
        if (mediaWithdrawnDepth++ == 0) setMedia(false);
    }
    refreshMountIfStale();
    return true;
}

void unlock() {
    if (!sdMutex) return;
    if (mediaWithdrawnDepth > 0 && --mediaWithdrawnDepth == 0) {
        // Re-present it; the host re-reads the FAT and sees our changes.
        setMedia(true);
    }
    xSemaphoreGive(sdMutex);
}

void refreshHostView() {
    if (!sdMutex) return;
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000)) != pdTRUE) return;
    setMedia(false);
    xSemaphoreGive(sdMutex);
    // Give the host time to notice the removal before offering it back.
    delay(600);
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        setMedia(true);
        xSemaphoreGive(sdMutex);
    }
    log("[usb] media re-presented to host");
}

}  // namespace storage
