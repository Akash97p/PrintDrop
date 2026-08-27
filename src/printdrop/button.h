#pragma once
#include <Arduino.h>

// Factory-reset button.
// Short press (< FACTORY_MS) -> eject/refresh printer view.
// Long press >= 5 s -> clear Wi-Fi + auth (NVS) and reboot.
// Active-low with internal pull-up by default (PRINTDROP_BUTTON_*).
namespace button {

using ShortPressCb = void (*)();
using LongPressCb  = void (*)();

void begin(ShortPressCb onShort, LongPressCb onLong);
void loop();

}  // namespace button
