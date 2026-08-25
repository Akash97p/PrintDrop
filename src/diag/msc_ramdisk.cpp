// USB mass storage self-test, backed by a small FAT12 image in RAM.
//
// Purpose: prove the USB side of the project independently of the SD card.
// If this firmware makes a drive appear on the host, then the board, the cable,
// the TinyUSB configuration and the MSC callbacks are all correct, and any
// remaining failure is in the SD card wiring.

#include <Arduino.h>
#include <USB.h>
#include <USBMSC.h>
#include "../common/reflash_hatch.h"

static const uint16_t kSectorSize  = 512;
static const uint16_t kSectorCount = 192;   // 96 KB volume
static const uint16_t kRootDirSector = 2;
static const uint16_t kFatSector     = 1;
static const uint16_t kFirstDataSector = 3;

static uint8_t* disk = nullptr;
USBMSC msc;

static void logf(const char* fmt, ...) {
    char buf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    UART_CONSOLE.println(buf);
}

static const char kReadme[] =
    "ESP32-S3 USB MSC self-test.\r\n"
    "\r\n"
    "This volume lives in the ESP32's RAM, not on the SD card.\r\n"
    "Seeing it means USB mass storage works on this board:\r\n"
    "TinyUSB is active, the descriptors enumerate, and the read\r\n"
    "and write callbacks are being served.\r\n"
    "\r\n"
    "If the SD card still does not appear with the main firmware,\r\n"
    "the fault is in the SD wiring, not in the USB code.\r\n";

static void buildFat12() {
    memset(disk, 0, (size_t)kSectorSize * kSectorCount);

    // --- Boot sector -------------------------------------------------------
    uint8_t* bs = disk;
    bs[0] = 0xEB; bs[1] = 0x3C; bs[2] = 0x90;          // jump
    memcpy(bs + 3, "MSDOS5.0", 8);                     // OEM name
    bs[11] = kSectorSize & 0xFF;                       // bytes per sector
    bs[12] = kSectorSize >> 8;
    bs[13] = 1;                                        // sectors per cluster
    bs[14] = 1; bs[15] = 0;                            // reserved sectors
    bs[16] = 1;                                        // number of FATs
    bs[17] = 16; bs[18] = 0;                           // root dir entries
    bs[19] = kSectorCount & 0xFF;                      // total sectors (16-bit)
    bs[20] = kSectorCount >> 8;
    bs[21] = 0xF8;                                     // media descriptor
    bs[22] = 1; bs[23] = 0;                            // sectors per FAT
    bs[24] = 1; bs[25] = 0;                            // sectors per track
    bs[26] = 1; bs[27] = 0;                            // heads
    bs[38] = 0x29;                                     // extended boot signature
    bs[39] = 0x34; bs[40] = 0x12; bs[41] = 0x78; bs[42] = 0x56;  // volume id
    memcpy(bs + 43, "ESP32RAMDSK", 11);                // volume label
    memcpy(bs + 54, "FAT12   ", 8);                    // fs type
    bs[510] = 0x55; bs[511] = 0xAA;

    // --- FAT ---------------------------------------------------------------
    // Entry 0 = media descriptor, entry 1 = EOC, entry 2 = EOC (README's only
    // cluster). FAT12 packs three nibbles per entry, two entries per 3 bytes.
    uint8_t* fat = disk + (size_t)kFatSector * kSectorSize;
    fat[0] = 0xF8; fat[1] = 0xFF; fat[2] = 0xFF;       // entries 0 and 1
    fat[3] = 0xFF; fat[4] = 0x0F;                      // entry 2 = 0xFFF (EOC)

    // --- Root directory ----------------------------------------------------
    uint8_t* root = disk + (size_t)kRootDirSector * kSectorSize;

    memcpy(root, "ESP32RAMDSK", 11);                   // volume label entry
    root[11] = 0x08;

    uint8_t* e = root + 32;                            // README.TXT
    memcpy(e, "README  TXT", 11);
    e[11] = 0x20;                                      // archive
    e[22] = 0x00; e[23] = 0x60;                        // time
    e[24] = 0x21; e[25] = 0x50;                        // date
    e[26] = 2; e[27] = 0;                              // first cluster
    uint32_t len = sizeof(kReadme) - 1;
    e[28] = len & 0xFF;
    e[29] = (len >> 8) & 0xFF;
    e[30] = (len >> 16) & 0xFF;
    e[31] = (len >> 24) & 0xFF;

    // --- File data ---------------------------------------------------------
    memcpy(disk + (size_t)kFirstDataSector * kSectorSize, kReadme, len);
}

static int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    if (lba >= kSectorCount) return -1;
    uint32_t maxLen = (uint32_t)kSectorSize * (kSectorCount - lba) - offset;
    if (bufsize > maxLen) bufsize = maxLen;
    memcpy(buffer, disk + (size_t)lba * kSectorSize + offset, bufsize);
    return bufsize;
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    if (lba >= kSectorCount) return -1;
    uint32_t maxLen = (uint32_t)kSectorSize * (kSectorCount - lba) - offset;
    if (bufsize > maxLen) bufsize = maxLen;
    memcpy(disk + (size_t)lba * kSectorSize + offset, buffer, bufsize);
    return bufsize;
}

static bool onStartStop(uint8_t power_condition, bool start, bool load_eject) {
    logf("[USB] start/stop power=%u start=%u eject=%u", power_condition, start, load_eject);
    return true;
}

void setup() {
    UART_CONSOLE.begin(115200);
    delay(1500);

    logf("");
    logf("=== ESP32-S3 USB MSC RAM-disk self-test ===");
    logf("Build: " __DATE__ " " __TIME__);
    logf("ARDUINO_USB_MODE=%d  CDC_ON_BOOT=%d",
         (int)ARDUINO_USB_MODE, (int)ARDUINO_USB_CDC_ON_BOOT);

    disk = (uint8_t*)malloc((size_t)kSectorSize * kSectorCount);
    if (!disk) {
        logf("FATAL: could not allocate %u bytes for the RAM disk.",
             (unsigned)kSectorSize * kSectorCount);
        while (true) delay(1000);
    }
    buildFat12();
    logf("RAM disk ready: %u sectors x %u bytes", kSectorCount, kSectorSize);

    USB.onEvent([](void* arg, esp_event_base_t base, int32_t id, void* data) {
        if (base != ARDUINO_USB_EVENTS) return;
        switch (id) {
            case ARDUINO_USB_STARTED_EVENT: logf("[USB] started");   break;
            case ARDUINO_USB_STOPPED_EVENT: logf("[USB] stopped");   break;
            case ARDUINO_USB_SUSPEND_EVENT: logf("[USB] suspended"); break;
            case ARDUINO_USB_RESUME_EVENT:  logf("[USB] resumed");   break;
            default: break;
        }
    });

    // Windows caches configuration descriptors per VID/PID. The stock
    // 303A:1001 is already bound to the USB-Serial-JTAG driver on this host, so
    // reusing it made the added MSC interface fail to appear. A distinct PID
    // forces a clean enumeration.
    USB.VID(0x303A);
    USB.PID(0x4002);
    USB.productName("ESP32-S3 RAM Disk");
    USB.manufacturerName("Espressif");
    USB.usbClass(TUSB_CLASS_UNSPECIFIED);
    USB.usbSubClass(0);
    USB.usbProtocol(0);

    msc.vendorID("ESP32");
    msc.productID("RAMDISK");
    msc.productRevision("1.0");
    msc.onRead(onRead);
    msc.onWrite(onWrite);
    msc.onStartStop(onStartStop);
    msc.mediaPresent(true);
    msc.begin(kSectorCount, kSectorSize);

    USB.begin();
    logf("[USB] MSC started - a 96 KB drive should appear on the host.");
}

void loop() {
    reflashHatchPoll();
    delay(50);
}
