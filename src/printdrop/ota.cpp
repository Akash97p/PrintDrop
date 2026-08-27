#include "ota.h"
#include <Update.h>
#include <ArduinoJson.h>
#ifdef USE_SDIO
#include <SD_MMC.h>
#define OTA_FS SD_MMC
#else
#include <SD.h>
#define OTA_FS SD
#endif
#include "config.h"
#include "storage.h"

#if PRINTDROP_ENABLE_OTA == 0
namespace ota {
void begin() {}
void loop() {}
bool checkSD(String*, String*) { return false; }
bool triggerSDUpdate(String* e) { if(e) *e="OTA disabled"; return false; }
bool beginUpdate(size_t, String* e){ if(e) *e="OTA disabled"; return false; }
bool writeUpdate(uint8_t*, size_t, String*){ return false; }
bool endUpdate(String* e){ if(e) *e="OTA disabled"; return false; }
bool abortUpdate(){ return false; }
bool isUpdating(){ return false; }
String currentVersion(){ return PRINTDROP_VERSION; }
}
#else
namespace ota {
namespace {

bool updating = false;

}

String currentVersion() { return PRINTDROP_VERSION; }
bool isUpdating() { return updating; }

void begin() {
    // Nothing to init; SD FS already mounted by storage.
}

void loop() {
    // Polling for SD OTA is done via checkSD() called from web status or main loop.
}

bool checkSD(String* outVer, String* outNotes) {
    if (!storage::cardMounted()) return false;
    storage::Guard g(false, 3000);
    if (!g.ok()) return false;
    if (!OTA_FS.exists(OTA_SD_FIRMWARE_PATH)) return false;
    if (!OTA_FS.exists(OTA_SD_META_PATH)) return false;
    File meta = OTA_FS.open(OTA_SD_META_PATH, FILE_READ);
    if (!meta) return false;
    String json = meta.readString();
    meta.close();
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return false;
    String ver = doc["version"] | "";
    if (ver.isEmpty()) return false;
    if (ver == PRINTDROP_VERSION) return false; // not newer (simple string compare)
    if (outVer) *outVer = ver;
    if (outNotes) *outNotes = doc["notes"] | "";
    return true;
}

bool triggerSDUpdate(String* err) {
    if (!storage::cardMounted()) { if(err) *err="No SD card"; return false; }
    storage::Guard g(false, 5000);
    if (!g.ok()) { if(err) *err="Card busy"; return false; }
    if (!OTA_FS.exists(OTA_SD_FIRMWARE_PATH)) { if(err) *err="firmware.bin not found"; return false; }
    File f = OTA_FS.open(OTA_SD_FIRMWARE_PATH, FILE_READ);
    if (!f) { if(err) *err="Cannot open firmware.bin"; return false; }
    size_t sz = f.size();
    if (sz == 0) { f.close(); if(err) *err="Empty firmware"; return false; }
    if (!Update.begin(sz)) { f.close(); if(err) *err=String("Update.begin failed: ") + Update.errorString(); return false; }
    updating = true;
    uint8_t buf[4096];
    while (f.available()) {
        size_t r = f.read(buf, sizeof(buf));
        if (r == 0) break;
        if (Update.write(buf, r) != r) { f.close(); Update.abort(); updating=false; if(err) *err=String("Write failed: ")+Update.errorString(); return false; }
    }
    f.close();
    if (!Update.end(true)) { updating=false; if(err) *err=String("Update.end failed: ")+Update.errorString(); return false; }
    updating=false;
    return true;
}

bool beginUpdate(size_t totalSize, String* err) {
    if (updating) { if(err) *err="Already updating"; return false; }
    if (!Update.begin(totalSize)) { if(err) *err=String("Update.begin failed: ")+Update.errorString(); return false; }
    updating = true;
    return true;
}

bool writeUpdate(uint8_t* data, size_t len, String* err) {
    if (!updating) { if(err) *err="Not in update"; return false; }
    if (Update.write(data, len) != len) { if(err) *err=String("Write failed: ")+Update.errorString(); return false; }
    return true;
}

bool endUpdate(String* err) {
    if (!updating) { if(err) *err="Not in update"; return false; }
    bool ok = Update.end(true);
    updating = false;
    if (!ok) { if(err) *err=String("Update.end failed: ")+Update.errorString(); return false; }
    return true;
}

bool abortUpdate() {
    if (updating) Update.abort();
    updating = false;
    return true;
}

}  // namespace ota
#endif
