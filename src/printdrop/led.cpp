#include "led.h"
#include "config.h"

namespace led {
namespace {

bool     enabled = false;
bool     busy    = false;
bool     error   = false;
uint32_t idlePeriod = 2000;
uint32_t lastToggle = 0;
bool     outState   = false;

inline void writeLed(bool on) {
#if PRINTDROP_LED_PIN >= 0
    const bool lvl = PRINTDROP_LED_ACTIVE_HIGH ? on : !on;
    digitalWrite(PRINTDROP_LED_PIN, lvl ? HIGH : LOW);
#else
    (void)on;
#endif
}

}  // namespace

bool isEnabled() { return enabled; }
void setIdlePeriod(uint32_t ms) { idlePeriod = ms ? ms : 2000; }
void setActivity(bool b) { busy = b; }
void setError(bool e) { error = e; }

void begin() {
#if PRINTDROP_LED_PIN < 0
    enabled = false;
    return;
#else
    enabled = true;
    pinMode(PRINTDROP_LED_PIN, OUTPUT);
    writeLed(false);
    lastToggle = millis();
    outState = false;
#endif
}

void loop() {
    if (!enabled) return;
    const uint32_t now = millis();
    if (error) {
        // Error: fast double-blink (100 ms on / 100 ms off / 100 ms on / 700 ms off)
        const uint32_t phase = now % 1000;
        bool on = (phase < 100) || (phase >= 200 && phase < 300);
        if (on != outState) { outState = on; writeLed(outState); }
        return;
    }
    if (busy) {
        // Activity: 200 ms period, 50% duty
        if (now - lastToggle >= 100) {
            lastToggle = now;
            outState = !outState;
            writeLed(outState);
        }
        return;
    }
    // Idle: 2 s period, 100 ms pulse (matches spec) — default 2000, configurable to 1000
    const uint32_t onTime = 100;
    const uint32_t phase = now % idlePeriod;
    bool on = phase < onTime;
    if (on != outState) { outState = on; writeLed(outState); }
}

}  // namespace led
