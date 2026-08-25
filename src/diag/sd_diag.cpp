// SD card diagnostic firmware for ESP32-S3-DevKitC-1.
// Leaves USB in Serial/JTAG mode so the board stays flashable while testing.

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include "../common/reflash_hatch.h"

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

SPIClass sdSPI;

// Mirror every line to the USB/JTAG console and to UART0, so the board can be
// watched from whichever of the two cables the user has open.
static void out(const String& s) {
    Serial.println(s);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial0.println(s);
#endif
}

static void outf(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    out(String(buf));
}

static void banner(const char* title) {
    out("");
    out("==================================================");
    out(String("  ") + title);
    out("==================================================");
}

static void reportChip() {
    banner("CHIP / BOARD");
    esp_chip_info_t info;
    esp_chip_info(&info);
    uint32_t flashSize = 0;
    esp_flash_get_size(NULL, &flashSize);

    outf("Chip model    : %d (cores=%d, rev=%d)", (int)info.model, info.cores, info.revision);
    outf("Flash size    : %u bytes (%u MB)", flashSize, flashSize / (1024 * 1024));
    outf("PSRAM size    : %u bytes", (unsigned)ESP.getPsramSize());
    outf("Free heap     : %u bytes", (unsigned)ESP.getFreeHeap());
    outf("SDK version   : %s", ESP.getSdkVersion());
#ifdef ARDUINO_USB_MODE
    outf("ARDUINO_USB_MODE        : %d", ARDUINO_USB_MODE);
#else
    out("ARDUINO_USB_MODE        : (undefined)");
#endif
    outf("ARDUINO_USB_CDC_ON_BOOT : %d", (int)ARDUINO_USB_CDC_ON_BOOT);
}

// --- Low level SPI probe -----------------------------------------------------
// Talks raw SD SPI-mode commands so we can tell "nothing is wired up" apart from
// "card answers but the Arduino SD driver refuses to mount it".

static uint8_t sdCommand(uint8_t cmd, uint32_t arg, uint8_t crc) {
    sdSPI.transfer(0x40 | cmd);
    sdSPI.transfer((arg >> 24) & 0xFF);
    sdSPI.transfer((arg >> 16) & 0xFF);
    sdSPI.transfer((arg >> 8) & 0xFF);
    sdSPI.transfer(arg & 0xFF);
    sdSPI.transfer(crc);
    // R1 arrives within 8 polls; 0xFF means the card is still busy / silent.
    for (int i = 0; i < 10; ++i) {
        uint8_t r = sdSPI.transfer(0xFF);
        if (!(r & 0x80)) return r;
    }
    return 0xFF;
}

static void probePins() {
    banner("PIN CONTINUITY PROBE");
    outf("Configured pins: CS=%d MISO=%d MOSI=%d CLK=%d",
         SD_CS_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CLK_PIN);

    // A wired, powered card holds MISO high (open-drain + pull-up) when idle.
    pinMode(SD_MISO_PIN, INPUT_PULLUP);
    delay(5);
    int misoPullup = digitalRead(SD_MISO_PIN);
    pinMode(SD_MISO_PIN, INPUT_PULLDOWN);
    delay(5);
    int misoPulldown = digitalRead(SD_MISO_PIN);
    pinMode(SD_MISO_PIN, INPUT);

    outf("MISO with pull-up   : %d (expect 1)", misoPullup);
    outf("MISO with pull-down : %d", misoPulldown);
    if (misoPullup == 1 && misoPulldown == 1) {
        out("  -> MISO is driven HIGH externally: card present and powered.");
    } else if (misoPullup == 1 && misoPulldown == 0) {
        out("  -> MISO floats: nothing is driving it. Card missing, unpowered,");
        out("     or MISO/3V3/GND not actually connected.");
    } else {
        out("  -> MISO stuck LOW: shorted to GND or wrong pin.");
    }
}

static void probeRawSD() {
    banner("RAW SD SPI PROBE");

    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);
    sdSPI.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    sdSPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));

    // 80 idle clocks with CS high put the card into native SPI mode.
    digitalWrite(SD_CS_PIN, HIGH);
    for (int i = 0; i < 10; ++i) sdSPI.transfer(0xFF);

    digitalWrite(SD_CS_PIN, LOW);
    uint8_t r1 = sdCommand(0, 0x00000000, 0x95);  // CMD0 GO_IDLE_STATE
    outf("CMD0 (GO_IDLE_STATE) response: 0x%02X", r1);
    if (r1 == 0x01) {
        out("  -> Card responded correctly and is in idle state. Wiring is good.");
    } else if (r1 == 0xFF) {
        out("  -> No response at all (MISO silent). Check MISO, CS, CLK, MOSI, 3V3, GND.");
    } else {
        out("  -> Unexpected response; card is talking but not idling as expected.");
    }

    if (r1 == 0x01) {
        uint8_t r8 = sdCommand(8, 0x000001AA, 0x87);  // CMD8 SEND_IF_COND
        uint8_t trailer[4];
        for (int i = 0; i < 4; ++i) trailer[i] = sdSPI.transfer(0xFF);
        outf("CMD8 (SEND_IF_COND) response: 0x%02X  trailer: %02X %02X %02X %02X",
             r8, trailer[0], trailer[1], trailer[2], trailer[3]);
        if (r8 == 0x01 && trailer[3] == 0xAA) {
            out("  -> SDHC/SDXC v2 card, 3.3V compatible.");
        } else if (r8 & 0x04) {
            out("  -> Illegal command: older SDSC v1 card.");
        }
    }

    digitalWrite(SD_CS_PIN, HIGH);
    sdSPI.transfer(0xFF);
    sdSPI.endTransaction();
    sdSPI.end();
}

// --- Arduino SD driver -------------------------------------------------------

static const char* cardTypeName(uint8_t t) {
    switch (t) {
        case CARD_NONE:    return "NONE";
        case CARD_MMC:     return "MMC";
        case CARD_SD:      return "SDSC";
        case CARD_SDHC:    return "SDHC/SDXC";
        default:           return "UNKNOWN";
    }
}

static void hexdump(const uint8_t* buf, size_t len) {
    char line[80];
    for (size_t i = 0; i < len; i += 16) {
        int n = snprintf(line, sizeof(line), "  %04X: ", (unsigned)i);
        for (size_t j = 0; j < 16 && i + j < len; ++j) {
            n += snprintf(line + n, sizeof(line) - n, "%02X ", buf[i + j]);
        }
        out(String(line));
    }
}

static void listRoot() {
    File root = SD.open("/");
    if (!root) {
        out("Could not open root directory.");
        return;
    }
    out("Root directory:");
    int count = 0;
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        outf("  %-30s %s %u bytes",
             f.name(), f.isDirectory() ? "<DIR>" : "     ", (unsigned)f.size());
        if (++count >= 25) {
            out("  ... (truncated)");
            break;
        }
    }
    if (count == 0) out("  (empty)");
    root.close();
}

// Walks every candidate clock and verifies each one with real reads, rather
// than trusting the first speed that happens to mount. A level-shifted module
// on jumper wires will often enumerate at 20 MHz but corrupt data.
//
// NOTE: this measures the *running* clock, not cold initialisation. The card is
// identified by the 400 kHz pass and stays initialised for the rest of the
// sweep, so a speed passing here is safe to raise the clock to -- but not safe
// to call SD.begin() with on a cold card. See mountSD() in main.cpp.
static uint32_t findBestSpeed() {
    banner("SPI SPEED SWEEP");
    out("Each speed is mounted, then verified by reading sector 0 twice and");
    out("comparing against a 400 kHz reference read.");
    out("");
    out("Measures sustained clock only. Card identification always happens at");
    out("400 kHz first, as the SD specification requires.");
    out("");

    const uint32_t freqs[] = {400000, 1000000, 4000000, 8000000,
                              10000000, 16000000, 20000000, 25000000};
    uint8_t reference[512];
    bool haveReference = false;
    uint32_t best = 0;

    for (uint32_t f : freqs) {
        sdSPI.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
        if (!SD.begin(SD_CS_PIN, sdSPI, f)) {
            outf("%8u Hz : mount FAILED", f);
            SD.end();
            sdSPI.end();
            delay(100);
            continue;
        }

        uint8_t buf[512];
        bool readOk = SD.readRAW(buf, 0);
        bool match = true;
        if (readOk) {
            if (!haveReference) {
                memcpy(reference, buf, 512);
                haveReference = true;
            } else {
                match = (memcmp(reference, buf, 512) == 0);
            }
        }

        // Time a 64 KB sequential read to get a usable throughput figure.
        uint32_t bytes = 0, t0 = millis();
        for (uint32_t sec = 0; sec < 128; ++sec) {
            if (!SD.readRAW(buf, sec)) { readOk = false; break; }
            bytes += 512;
        }
        uint32_t dt = millis() - t0;
        uint32_t kbps = dt ? (bytes / dt) : 0;

        if (readOk && match) {
            outf("%8u Hz : OK    %u KB/s", f, kbps);
            best = f;
        } else if (!readOk) {
            outf("%8u Hz : read FAILED", f);
        } else {
            outf("%8u Hz : DATA MISMATCH - unreliable", f);
        }

        SD.end();
        sdSPI.end();
        delay(100);
    }

    out("");
    if (best) outf("Highest verified speed: %u Hz", best);
    else      out("No speed produced verified reads.");
    return best;
}

static bool mountAndReport() {
    uint32_t best = findBestSpeed();
    if (!best) return false;

    banner("ARDUINO SD DRIVER");
    sdSPI.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    if (!SD.begin(SD_CS_PIN, sdSPI, best)) {
        out("Remount at the chosen speed failed.");
        return false;
    }

    uint8_t type = SD.cardType();
    uint64_t cardSize = SD.cardSize();
    uint32_t secSize = SD.sectorSize();
    uint32_t numSec = SD.numSectors();

    outf("Mount speed   : %u Hz", best);
    outf("Card type     : %s (%u)", cardTypeName(type), type);
    outf("Card size     : %llu bytes (%llu MB)", cardSize, cardSize / (1024ULL * 1024ULL));
    outf("Sector size   : %u bytes", secSize);
    outf("Sector count  : %u", numSec);
    outf("Raw capacity  : %llu bytes", (uint64_t)numSec * secSize);
    outf("FS total      : %llu bytes", SD.totalBytes());
    outf("FS used       : %llu bytes", SD.usedBytes());

    if (secSize == 0 || numSec == 0) {
        out("");
        out("WARNING: sectorSize/numSectors is zero. USB MSC cannot work like this.");
    }

    banner("RAW SECTOR 0 (MBR)");
    uint8_t* sector = (uint8_t*)malloc(secSize ? secSize : 512);
    if (!sector) {
        out("Out of memory for sector buffer.");
        return true;
    }
    if (SD.readRAW(sector, 0)) {
        hexdump(sector, 96);
        out("  ...");
        outf("Boot signature: %02X %02X (expect 55 AA)", sector[510], sector[511]);
        if (sector[510] == 0x55 && sector[511] == 0xAA) {
            const uint8_t* pe = sector + 0x1BE;
            uint32_t lbaStart = pe[8] | (pe[9] << 8) | (pe[10] << 16) | ((uint32_t)pe[11] << 24);
            uint32_t lbaCount = pe[12] | (pe[13] << 8) | (pe[14] << 16) | ((uint32_t)pe[15] << 24);
            outf("Partition 1: type=0x%02X boot=0x%02X startLBA=%u sectors=%u",
                 pe[4], pe[0], lbaStart, lbaCount);
            switch (pe[4]) {
                case 0x0B: case 0x0C: out("  -> FAT32. Good for USB MSC."); break;
                case 0x06: case 0x0E: out("  -> FAT16."); break;
                case 0x07: out("  -> exFAT/NTFS. Windows will read it over MSC, ESP32 will not."); break;
                default: outf("  -> partition type 0x%02X", pe[4]);
            }
        } else {
            out("  -> No MBR signature. Card may be superfloppy-formatted or unreadable.");
        }
    } else {
        out("SD.readRAW(sector 0) FAILED - USB MSC reads would fail too.");
    }
    free(sector);

    banner("FILESYSTEM");
    listRoot();
    return true;
}

void setup() {
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial0.begin(115200);
#endif
    // Give the host time to attach to the CDC/JTAG port before the first print.
    delay(3000);

    banner("ESP32-S3 SD CARD DIAGNOSTIC");
    out("Build: " __DATE__ " " __TIME__);

    reportChip();
    probePins();
    probeRawSD();
    mountAndReport();

    banner("DIAGNOSTIC COMPLETE");
}

void loop() {
    reflashHatchPoll();
    delay(50);
}
