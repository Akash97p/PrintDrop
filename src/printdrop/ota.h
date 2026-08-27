#pragma once
#include <Arduino.h>

// OTA via HTTP POST /api/ota (Update) and via SD file detection.
// Call ota::begin() after storage::begin() (needs SD FS). Poll ota::loop() or
// call ota::checkSD() periodically to detect firmware.bin on SD.
namespace ota {

void begin();
void loop();

// SD OTA detection: returns true if firmware.bin + firmware.json with newer version found.
// Caller can show popup and call triggerSDUpdate().
bool checkSD(String* outVersion = nullptr, String* outNotes = nullptr);
bool triggerSDUpdate(String* err = nullptr);

// HTTP upload handler helpers
bool beginUpdate(size_t totalSize, String* err);
bool writeUpdate(uint8_t* data, size_t len, String* err);
bool endUpdate(String* err);
bool abortUpdate();

bool isUpdating();
String currentVersion();

}  // namespace ota
