#pragma once

// HTTP server: static UI from LittleFS plus the JSON file API.

namespace web {
bool begin();
void loop();
bool rebootRequested();
}  // namespace web
