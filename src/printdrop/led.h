#pragma once
#include <Arduino.h>

// Heartbeat + activity LED.
// Idle: slow blink (2 s period, 100 ms on). Activity: rapid blink (200 ms period).
// Call led::loop() from main loop; call led::setActivity(true) while SD/USB busy.
namespace led {

void begin();
void loop();
void setActivity(bool busy);
void setError(bool err);
void setIdlePeriod(uint32_t ms);
bool isEnabled();

}  // namespace led
