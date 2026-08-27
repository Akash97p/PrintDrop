#include "button.h"
#include "config.h"

#if PRINTDROP_BUTTON_PIN >= 0
namespace button {
namespace {

ShortPressCb shortCb = nullptr;
LongPressCb  longCb  = nullptr;

uint32_t pressStart = 0;
bool     wasPressed = false;
bool     longFired  = false;
uint32_t lastDebounce = 0;
bool     lastRaw = false;

inline bool rawPressed() {
    int v = digitalRead(PRINTDROP_BUTTON_PIN);
    return PRINTDROP_BUTTON_ACTIVE_LOW ? (v == LOW) : (v == HIGH);
}

}  // namespace

void begin(ShortPressCb onShort, LongPressCb onLong) {
    shortCb = onShort;
    longCb  = onLong;
    pinMode(PRINTDROP_BUTTON_PIN,
            PRINTDROP_BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
    lastRaw = rawPressed();
    wasPressed = lastRaw;
    if (wasPressed) pressStart = millis();
}

void loop() {
    bool raw = rawPressed();
    uint32_t now = millis();

    // Debounce 30 ms
    if (raw != lastRaw) {
        lastDebounce = now;
        lastRaw = raw;
    }
    if (now - lastDebounce < 30) return;

    bool pressed = raw;

    if (pressed && !wasPressed) {
        // Press start
        wasPressed = true;
        pressStart = now;
        longFired = false;
    } else if (!pressed && wasPressed) {
        // Release
        uint32_t dur = now - pressStart;
        wasPressed = false;
        if (!longFired && dur >= BUTTON_EJECT_MS && dur < BUTTON_FACTORY_RESET_MS) {
            if (shortCb) shortCb();
        }
        longFired = false;
    } else if (pressed && wasPressed && !longFired && (now - pressStart >= BUTTON_FACTORY_RESET_MS)) {
        longFired = true;
        if (longCb) longCb();
    }
}

}  // namespace button
#else
namespace button {
void begin(ShortPressCb, LongPressCb) {}
void loop() {}
}  // namespace button
#endif
