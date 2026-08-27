#pragma once
#include <Arduino.h>

// Minimal WebSocket progress broadcaster.
// Runs alongside the synchronous WebServer on port 81 to avoid blocking the
// main HTTP task. Clients connect to ws://<host>:81/ and receive JSON
// {"type":"progress","name":"...","pct":..,"rate":..} and status pushes.
namespace ws {

void begin();
void loop();
void broadcast(const String& json);
void broadcastProgress(const String& name, uint8_t pct, uint32_t rateBps, uint32_t etaSec);
void broadcastStatus(const String& statusJson);

bool hasClients();

}  // namespace ws
