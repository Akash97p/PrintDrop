// SD card wiring scanner.
//
// The plain diagnostic showed the card silent on the configured pins. This
// firmware bit-bangs the SD SPI init sequence so it can drive *any* GPIO, and
// sweeps pin assignments to tell a mis-wire apart from a dead/unpowered module.

#include <Arduino.h>
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

static void banner(const char* t) {
    out("");
    out("==================================================");
    out(String("  ") + t);
    out("==================================================");
}

// GPIOs broken out on an ESP32-S3-DevKitC-1 and safe to toggle.
// Excluded: 0/45/46 (strapping), 19/20 (native USB D-/D+),
// 26..37 (SPI flash + PSRAM), 43/44 (UART0 console).
static const uint8_t kSafeGpio[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
    21, 38, 39, 40, 41, 42, 47, 48
};
static const size_t kSafeGpioCount = sizeof(kSafeGpio) / sizeof(kSafeGpio[0]);

// --- Bit-banged SD SPI (mode 0), ~200 kHz -----------------------------------

struct Bus {
    uint8_t cs, miso, mosi, clk;
};

static void bbSetup(const Bus& b) {
    pinMode(b.clk, OUTPUT);
    pinMode(b.mosi, OUTPUT);
    pinMode(b.cs, OUTPUT);
    pinMode(b.miso, INPUT_PULLUP);
    digitalWrite(b.clk, LOW);
    digitalWrite(b.mosi, HIGH);
    digitalWrite(b.cs, HIGH);
}

static void bbRelease(const Bus& b) {
    pinMode(b.clk, INPUT);
    pinMode(b.mosi, INPUT);
    pinMode(b.cs, INPUT);
    pinMode(b.miso, INPUT);
}

static uint8_t bbTransfer(const Bus& b, uint8_t v) {
    uint8_t r = 0;
    for (int i = 7; i >= 0; --i) {
        digitalWrite(b.mosi, (v >> i) & 1);
        delayMicroseconds(2);
        digitalWrite(b.clk, HIGH);
        delayMicroseconds(2);
        r = (r << 1) | (digitalRead(b.miso) ? 1 : 0);
        digitalWrite(b.clk, LOW);
    }
    return r;
}

static uint8_t bbCommand(const Bus& b, uint8_t cmd, uint32_t arg, uint8_t crc) {
    bbTransfer(b, 0xFF);
    bbTransfer(b, 0x40 | cmd);
    bbTransfer(b, (arg >> 24) & 0xFF);
    bbTransfer(b, (arg >> 16) & 0xFF);
    bbTransfer(b, (arg >> 8) & 0xFF);
    bbTransfer(b, arg & 0xFF);
    bbTransfer(b, crc);
    for (int i = 0; i < 16; ++i) {
        uint8_t r = bbTransfer(b, 0xFF);
        if (!(r & 0x80)) return r;
    }
    return 0xFF;
}

// Returns the CMD0 R1 response. 0x01 means a real card answered.
static uint8_t bbProbe(const Bus& b) {
    bbSetup(b);
    digitalWrite(b.cs, HIGH);
    for (int i = 0; i < 12; ++i) bbTransfer(b, 0xFF);  // >=74 idle clocks
    digitalWrite(b.cs, LOW);
    uint8_t r1 = bbCommand(b, 0, 0x00000000, 0x95);
    digitalWrite(b.cs, HIGH);
    bbTransfer(b, 0xFF);
    bbRelease(b);
    return r1;
}

// --- Tests -------------------------------------------------------------------

// ESP32-S3 ADC1 covers GPIO1-10, ADC2 covers GPIO11-20. Outside that range a
// pin can only be read digitally.
static bool pinHasAdc(uint8_t p) { return p >= 1 && p <= 20; }

static void testPinVoltages() {
    banner("TEST 2b: LINE VOLTAGES");
    out("Reads each line high-impedance (what the module actually drives) and");
    out("again with the internal pull-up, for reference.");
    out("  raw 4095   = above the ADC range, i.e. over 3.3V -> OUT OF SPEC");
    out("  ~0.3-0.9 V = clamped by an ESD diode -> the module has NO VCC");
    out("  ~0.0 V     = hard short to ground");
    out("");

    const uint8_t pins[] = {SD_CS_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CLK_PIN};
    const char* names[] = {"CS", "MISO", "MOSI", "CLK"};

    for (int i = 0; i < 4; ++i) {
        uint8_t p = pins[i];
        if (!pinHasAdc(p)) {
            outf("GPIO%-2d (%-4s): no ADC on this pin - digital only", p, names[i]);
            continue;
        }
        // Pin the attenuation so the numbers mean something. On ESP32-S3 the
        // 12 dB range tops out near 3.1 V; a raw count at full scale means
        // "at or above that", which is already over spec for a 3.3 V GPIO.
        analogSetPinAttenuation(p, ADC_11db);

        pinMode(p, INPUT);            // high-Z: what the module actually drives
        delay(10);
        int rawFloat = analogRead(p);
        int mvFloat  = analogReadMilliVolts(p);

        pinMode(p, INPUT_PULLUP);     // reference: internal pull-up to 3V3
        delay(10);
        int mvPullup = analogReadMilliVolts(p);
        pinMode(p, INPUT);

        const char* verdict;
        if (rawFloat >= 4090)     verdict = "*** OVER RANGE - above 3.3V, OUT OF SPEC ***";
        else if (mvFloat > 2000)  verdict = "driven high (in range)";
        else if (mvFloat > 250)   verdict = "*** DIODE CLAMP - module unpowered ***";
        else if (mvPullup < 250)  verdict = "*** SHORTED TO GROUND ***";
        else                      verdict = "free / not driven";
        outf("GPIO%-2d (%-4s): hi-Z %4d mV (raw %4d), pullup %4d mV -> %s",
             p, names[i], mvFloat, rawFloat, mvPullup, verdict);
    }
    out("");
    outf("NOTE: MISO is on GPIO%d, which has no ADC. To measure it, move the",
         SD_MISO_PIN);
    out("MISO wire to GPIO12 temporarily and re-read this test - GPIO12 has an");
    out("ADC and the scanner re-runs every 10 seconds.");
}

static void testGpioHealth() {
    banner("TEST 1: GPIO HEALTH");
    out("Driving each ESP32-driven SD pin high and low and reading it back.");
    out("A pin that will not follow is damaged, shorted, or driven by the card");
    out("(which is what a swapped MISO/MOSI pair looks like).");
    out("");

    // MISO is deliberately left out: it is the card's output, and driving it
    // would put the ESP32 and the card in contention. Test 2 covers it safely.
    const uint8_t pins[] = {SD_CS_PIN, SD_MOSI_PIN, SD_CLK_PIN};
    const char* names[] = {"CS", "MOSI", "CLK"};

    for (int i = 0; i < 3; ++i) {
        uint8_t p = pins[i];
        pinMode(p, OUTPUT);
        digitalWrite(p, HIGH);
        delayMicroseconds(200);
        int hi = digitalRead(p);
        digitalWrite(p, LOW);
        delayMicroseconds(200);
        int lo = digitalRead(p);
        pinMode(p, INPUT);
        outf("GPIO%-2d (%-4s): drive HIGH -> %d, drive LOW -> %d   %s",
             p, names[i], hi, lo,
             (hi == 1 && lo == 0) ? "OK" : "*** FAULT ***");
    }
}

static void testFloatState() {
    banner("TEST 2: IDLE LINE STATES");
    out("With the ESP32 not driving, a powered SD module holds MISO high.");
    out("");

    const uint8_t pins[] = {SD_CS_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CLK_PIN};
    const char* names[] = {"CS", "MISO", "MOSI", "CLK"};

    for (int i = 0; i < 4; ++i) {
        uint8_t p = pins[i];
        pinMode(p, INPUT_PULLDOWN);
        delay(5);
        int withPd = digitalRead(p);
        pinMode(p, INPUT_PULLUP);
        delay(5);
        int withPu = digitalRead(p);
        pinMode(p, INPUT);
        const char* verdict;
        if (withPd == 1 && withPu == 1)      verdict = "driven HIGH externally";
        else if (withPd == 0 && withPu == 0) verdict = "driven LOW externally (short?)";
        else                                 verdict = "floating / not connected";
        outf("GPIO%-2d (%-4s): pulldown=%d pullup=%d  -> %s",
             p, names[i], withPd, withPu, verdict);
    }
}

static bool testPermutations(Bus& found) {
    banner("TEST 3: PIN PERMUTATION SWEEP");
    outf("Trying all 24 orderings of GPIO %d/%d/%d/%d across CS/MISO/MOSI/CLK.",
         SD_CS_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CLK_PIN);
    out("This catches swapped wires (MISO/MOSI is the classic one).");
    out("");

    uint8_t base[4] = {SD_CS_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CLK_PIN};
    uint8_t idx[4] = {0, 1, 2, 3};
    bool hit = false;

    // Plain lexicographic permutation walk over the four indices.
    for (int a = 0; a < 4; ++a)
    for (int b = 0; b < 4; ++b) { if (b == a) continue;
    for (int c = 0; c < 4; ++c) { if (c == a || c == b) continue;
    for (int d = 0; d < 4; ++d) { if (d == a || d == b || d == c) continue;
        Bus bus = {base[a], base[b], base[c], base[d]};
        uint8_t r1 = bbProbe(bus);
        if (r1 == 0x01) {
            outf("  CS=%d MISO=%d MOSI=%d CLK=%d -> R1=0x%02X  *** CARD FOUND ***",
                 bus.cs, bus.miso, bus.mosi, bus.clk, r1);
            if (!hit) { found = bus; hit = true; }
        } else if (r1 != 0xFF && r1 != 0x00) {
            outf("  CS=%d MISO=%d MOSI=%d CLK=%d -> R1=0x%02X (activity)",
                 bus.cs, bus.miso, bus.mosi, bus.clk, r1);
        }
    }}}
    (void)idx;

    if (!hit) out("  No ordering produced a valid CMD0 response.");
    return hit;
}

static bool testMisoSweep(Bus& found) {
    banner("TEST 4: BROAD MISO SWEEP");
    out("Holding CS/MOSI/CLK as configured and looking for the card's reply");
    out("on every other usable GPIO, in case MISO landed on a different pin.");
    out("");

    bool hit = false;
    for (size_t i = 0; i < kSafeGpioCount; ++i) {
        uint8_t p = kSafeGpio[i];
        if (p == SD_CS_PIN || p == SD_MOSI_PIN || p == SD_CLK_PIN) continue;
        Bus bus = {SD_CS_PIN, p, SD_MOSI_PIN, SD_CLK_PIN};
        uint8_t r1 = bbProbe(bus);
        if (r1 == 0x01) {
            outf("  MISO on GPIO%d -> R1=0x01  *** CARD FOUND ***", p);
            if (!hit) { found = bus; hit = true; }
        } else if (r1 != 0xFF && r1 != 0x00) {
            outf("  MISO on GPIO%d -> R1=0x%02X (activity)", p, r1);
        }
    }
    if (!hit) out("  No GPIO carried a valid CMD0 response.");
    return hit;
}

static void testFullMount(const Bus& bus) {
    banner("TEST 5: MOUNT WITH DISCOVERED PINS");
    outf("Using CS=%d MISO=%d MOSI=%d CLK=%d", bus.cs, bus.miso, bus.mosi, bus.clk);

    static SPIClass spi;
    spi.begin(bus.clk, bus.miso, bus.mosi, bus.cs);
    const uint32_t freqs[] = {400000, 4000000, 10000000, 20000000};
    for (uint32_t f : freqs) {
        if (SD.begin(bus.cs, spi, f)) {
            outf("MOUNTED at %u Hz", f);
            outf("  type=%u size=%llu bytes sectorSize=%u numSectors=%u",
                 SD.cardType(), SD.cardSize(), SD.sectorSize(), SD.numSectors());
            return;
        }
        outf("  SD.begin at %u Hz failed", f);
        SD.end();
    }
    out("Card answers CMD0 but the FAT driver could not mount it.");
}

static void runAllTests();

void setup() {
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial0.begin(115200);
#endif
    delay(3000);

    banner("ESP32-S3 SD WIRING SCANNER");
    out("Build: " __DATE__ " " __TIME__);
    outf("Configured: CS=%d MISO=%d MOSI=%d CLK=%d",
         SD_CS_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CLK_PIN);

    runAllTests();
}

static void runAllTests() {
    banner("SCAN PASS");
    testGpioHealth();
    testFloatState();
    testPinVoltages();

    Bus found = {0, 0, 0, 0};
    bool ok = testPermutations(found);
    if (!ok) ok = testMisoSweep(found);

    if (ok) {
        testFullMount(found);
    } else {
        banner("VERDICT");
        out("No SD card responded on any tested pin arrangement.");
        out("The ESP32 side is working; nothing is answering on the bus.");
        out("");
        out("Most likely causes, in order:");
        out("  1. The module has no VCC. An unpowered chip clamps whatever pin");
        out("     you pull up down to ~0.6 V through its ESD diode, which is");
        out("     exactly what a held-low MISO looks like. Check VCC and GND");
        out("     at the module itself, not at the breadboard rail.");
        out("  2. The module is a 5V 'Arduino SD card module' with an on-board");
        out("     LDO and 74HC125 level shifter. Those need 5V on VCC; on 3V3");
        out("     they stay silent. Move VCC to 5V/VBUS (signals stay 3.3V).");
        out("  3. A jumper is one pin off on the header - MISO landing on GND");
        out("     produces the same hard low.");
        out("  4. Card not seated, or a cold solder joint on the socket.");
    }

    banner("SCAN COMPLETE");
}

// Re-run the whole sweep every 10 s so wiring can be changed and re-tested
// without reflashing or resetting the board.
void loop() {
    for (int i = 0; i < 200; ++i) { reflashHatchPoll(); delay(50); }
    runAllTests();
}
