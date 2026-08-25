#include "web.h"

#include <Arduino.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <SD.h>
#include <WiFi.h>

#include "config.h"
#include "net.h"
#include "storage.h"

namespace web {
namespace {

WebServer server(80);
bool      wantReboot = false;

// Upload state. WebServer drives uploads through repeated callbacks, so the
// SD lock has to be held across them rather than scoped to one function.
File   uploadFile;
bool   uploadHoldsLock = false;
String uploadError;
String uploadName;
size_t uploadBytes = 0;

void log(const char* fmt, ...) {
    char buf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.println(buf);
}

// --- helpers ---------------------------------------------------------------

String jsonEscape(const String& s) {
    String o;
    o.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); ++i) {
        const char c = s[i];
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (static_cast<uint8_t>(c) < 0x20) {
                    char b[8];
                    snprintf(b, sizeof(b), "\\u%04x", c);
                    o += b;
                } else {
                    o += c;
                }
        }
    }
    return o;
}

// Normalises a client-supplied path and refuses anything that tries to escape
// the card root.
bool safePath(const String& in, String& out) {
    String p = in;
    p.trim();
    if (p.isEmpty()) { out = "/"; return true; }
    if (!p.startsWith("/")) p = "/" + p;
    if (p.indexOf("..") >= 0) return false;
    while (p.indexOf("//") >= 0) p.replace("//", "/");
    if (p.length() > 1 && p.endsWith("/")) p.remove(p.length() - 1);
    out = p;
    return true;
}

String joinPath(const String& dir, const String& name) {
    if (dir.isEmpty() || dir == "/") return "/" + name;
    return dir + "/" + name;
}

void sendJson(int code, const String& body) {
    server.sendHeader("Cache-Control", "no-store");
    server.send(code, "application/json", body);
}

void sendOk() { sendJson(200, "{\"ok\":true}"); }

void sendError(int code, const String& message) {
    sendJson(code, String("{\"ok\":false,\"error\":\"") + jsonEscape(message) + "\"}");
}

const char* contentTypeFor(const String& path) {
    if (path.endsWith(".html")) return "text/html";
    if (path.endsWith(".css"))  return "text/css";
    if (path.endsWith(".js"))   return "application/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".svg"))  return "image/svg+xml";
    if (path.endsWith(".ico"))  return "image/x-icon";
    if (path.endsWith(".png"))  return "image/png";
    if (path.endsWith(".webp")) return "image/webp";
    if (path.endsWith(".woff2"))return "font/woff2";
    return "text/plain";
}

// --- static UI from LittleFS ----------------------------------------------

bool serveFromLittleFS(String path) {
    if (path.endsWith("/")) path += "index.html";

    // Prefer a pre-compressed copy when one was shipped.
    const String gz = path + ".gz";
    if (LittleFS.exists(gz)) {
        File f = LittleFS.open(gz, "r");
        if (!f) return false;
        server.sendHeader("Content-Encoding", "gzip");
        server.sendHeader("Cache-Control", "public, max-age=86400");
        server.streamFile(f, contentTypeFor(path));
        f.close();
        return true;
    }
    if (LittleFS.exists(path)) {
        File f = LittleFS.open(path, "r");
        if (!f) return false;
        server.sendHeader("Cache-Control", "public, max-age=86400");
        server.streamFile(f, contentTypeFor(path));
        f.close();
        return true;
    }
    return false;
}

void handleNotFound() {
    if (serveFromLittleFS(server.uri())) return;
    // Single-page app: unknown non-API routes fall back to the shell.
    if (!server.uri().startsWith("/api/") && serveFromLittleFS("/index.html")) return;
    sendError(404, "Not found");
}

// --- API: status -----------------------------------------------------------

void handleStatus() {
    const uint64_t total = storage::totalBytes();
    const uint64_t used  = storage::usedBytes();
    const uint64_t freeB = total > used ? total - used : 0;

    String j = "{";
    j += "\"name\":\"" PRINTDROP_NAME "\",";
    j += "\"version\":\"" PRINTDROP_VERSION "\",";
    j += "\"hostname\":\"" + jsonEscape(net::hostname()) + "\",";
    j += "\"ip\":\"" + net::localIp().toString() + "\",";
    j += "\"ssid\":\"" + jsonEscape(net::currentSsid()) + "\",";
    j += "\"rssi\":" + String(net::rssi()) + ",";
    j += "\"mode\":\"" + String(net::isAccessPoint() ? "ap" : "sta") + "\",";
    j += "\"card\":{";
    j +=   "\"present\":" + String(storage::cardMounted() ? "true" : "false") + ",";
    j +=   "\"totalBytes\":" + String(total) + ",";
    j +=   "\"usedBytes\":" + String(used) + ",";
    j +=   "\"freeBytes\":" + String(freeB) + ",";
    j +=   "\"spiHz\":" + String(storage::spiFrequency());
    j += "},";
    j += "\"usb\":{";
    j +=   "\"hostPresent\":" + String(storage::usbHostPresent() ? "true" : "false") + ",";
    j +=   "\"mediaPresent\":" + String(storage::usbMediaPresent() ? "true" : "false");
    j += "},";
    j += "\"uptimeMs\":" + String(millis());
    j += "}";
    sendJson(200, j);
}

// --- API: listing ----------------------------------------------------------

void handleList() {
    String path;
    if (!safePath(server.arg("path"), path)) return sendError(400, "Invalid path");

    storage::Guard g(false);
    if (!g.ok()) return sendError(503, "Card busy");

    File dir = SD.open(path);
    if (!dir) return sendError(404, "No such folder");
    if (!dir.isDirectory()) { dir.close(); return sendError(400, "Not a folder"); }

    String j = "{\"path\":\"" + jsonEscape(path) + "\",\"entries\":[";
    bool first = true;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (!first) j += ",";
        first = false;
        String name = f.name();
        // Some cores hand back a full path; the UI only wants the leaf.
        const int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        j += "{\"name\":\"" + jsonEscape(name) + "\",";
        j += "\"dir\":" + String(f.isDirectory() ? "true" : "false") + ",";
        j += "\"size\":" + String((uint32_t)f.size()) + "}";
        f.close();
    }
    dir.close();
    j += "]}";
    sendJson(200, j);
}

// --- API: mutations --------------------------------------------------------

void removeRecursive(const String& path) {
    File f = SD.open(path);
    if (!f) return;
    if (!f.isDirectory()) { f.close(); SD.remove(path); return; }
    for (File child = f.openNextFile(); child; child = f.openNextFile()) {
        String name = child.name();
        const int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        const bool isDir = child.isDirectory();
        child.close();
        if (isDir) removeRecursive(joinPath(path, name));
        else       SD.remove(joinPath(path, name));
    }
    f.close();
    SD.rmdir(path);
}

void handleDelete() {
    String path;
    if (!safePath(server.arg("path"), path)) return sendError(400, "Invalid path");
    if (path == "/") return sendError(400, "Refusing to delete the card root");

    storage::Guard g(true);
    if (!g.ok()) return sendError(503, "Card busy");
    if (!SD.exists(path)) return sendError(404, "No such file");

    removeRecursive(path);
    if (SD.exists(path)) return sendError(500, "Delete failed");
    log("[web] deleted %s", path.c_str());
    sendOk();
}

void handleMkdir() {
    String path;
    if (!safePath(server.arg("path"), path)) return sendError(400, "Invalid path");
    if (path == "/") return sendError(400, "Invalid folder name");

    storage::Guard g(true);
    if (!g.ok()) return sendError(503, "Card busy");
    if (SD.exists(path)) return sendError(409, "Already exists");
    if (!SD.mkdir(path)) return sendError(500, "Could not create folder");
    sendOk();
}

void handleRename() {
    String from, to;
    if (!safePath(server.arg("from"), from) || !safePath(server.arg("to"), to)) {
        return sendError(400, "Invalid path");
    }
    if (from == "/" || to == "/") return sendError(400, "Invalid path");

    storage::Guard g(true);
    if (!g.ok()) return sendError(503, "Card busy");
    if (!SD.exists(from)) return sendError(404, "No such file");
    if (SD.exists(to))    return sendError(409, "Target already exists");
    if (!SD.rename(from, to)) return sendError(500, "Rename failed");
    sendOk();
}

void handleDownload() {
    String path;
    if (!safePath(server.arg("path"), path)) return sendError(400, "Invalid path");

    storage::Guard g(false, 20000);
    if (!g.ok()) return sendError(503, "Card busy");

    File f = SD.open(path, FILE_READ);
    if (!f) return sendError(404, "No such file");
    if (f.isDirectory()) { f.close(); return sendError(400, "Is a folder"); }

    String name = path.substring(path.lastIndexOf('/') + 1);
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    server.streamFile(f, "application/octet-stream");
    f.close();
}

void handleEject() {
    storage::refreshHostView();
    sendOk();
}

// --- API: upload -----------------------------------------------------------

void handleUploadData() {
    HTTPUpload& up = server.upload();

    if (up.status == UPLOAD_FILE_START) {
        uploadError = "";
        uploadBytes = 0;

        String dir;
        // Query-string arguments are parsed before the multipart body, so this
        // is readable here (see WebServer::_parseRequest).
        if (!safePath(server.arg("path"), dir)) { uploadError = "Invalid path"; return; }

        String name = up.filename;
        const int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        if (name.isEmpty()) { uploadError = "Missing filename"; return; }
        uploadName = joinPath(dir, name);

        if (!storage::lock(true, 20000)) { uploadError = "Card busy"; return; }
        uploadHoldsLock = true;

        if (SD.exists(uploadName)) SD.remove(uploadName);
        uploadFile = SD.open(uploadName, FILE_WRITE);
        if (!uploadFile) {
            uploadError = "Could not open file for writing";
            storage::unlock();
            uploadHoldsLock = false;
        }

    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (uploadFile && uploadError.isEmpty()) {
            if (uploadFile.write(up.buf, up.currentSize) != up.currentSize) {
                uploadError = "Write failed — card full?";
            } else {
                uploadBytes += up.currentSize;
            }
        }

    } else if (up.status == UPLOAD_FILE_END) {
        if (uploadFile) uploadFile.close();
        if (uploadHoldsLock) { storage::unlock(); uploadHoldsLock = false; }
        if (uploadError.isEmpty()) {
            log("[web] uploaded %s (%u bytes)", uploadName.c_str(), (unsigned)uploadBytes);
        }

    } else if (up.status == UPLOAD_FILE_ABORTED) {
        if (uploadFile) uploadFile.close();
        // Do not leave a truncated file that looks like a valid print job.
        if (!uploadName.isEmpty() && SD.exists(uploadName)) SD.remove(uploadName);
        if (uploadHoldsLock) { storage::unlock(); uploadHoldsLock = false; }
        uploadError = "Upload aborted";
    }
}

void handleUploadDone() {
    if (!uploadError.isEmpty()) return sendError(500, uploadError);
    String j = "{\"ok\":true,\"name\":\"" + jsonEscape(uploadName) + "\",";
    j += "\"size\":" + String((uint32_t)uploadBytes) + "}";
    sendJson(200, j);
}

// --- API: Wi-Fi ------------------------------------------------------------

void handleWifiScan() {
    const int n = WiFi.scanNetworks();
    String j = "{\"networks\":[";
    for (int i = 0; i < n && i < 25; ++i) {
        if (i) j += ",";
        j += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",";
        j += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
        j += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
    }
    j += "]}";
    WiFi.scanDelete();
    sendJson(200, j);
}

void handleWifiSave() {
    net::Config c = net::config();
    if (server.hasArg("ssid"))     c.ssid      = server.arg("ssid");
    if (server.hasArg("password")) c.password  = server.arg("password");
    if (server.hasArg("hostname")) c.hostname  = server.arg("hostname");
    if (server.hasArg("useStatic"))c.useStatic = server.arg("useStatic") == "true" ||
                                                 server.arg("useStatic") == "1";
    if (server.hasArg("ip"))       c.ip      = server.arg("ip");
    if (server.hasArg("gw"))       c.gateway = server.arg("gw");
    if (server.hasArg("mask"))     c.mask    = server.arg("mask");
    if (server.hasArg("dns"))      c.dns     = server.arg("dns");

    if (c.ssid.isEmpty()) return sendError(400, "SSID is required");
    if (!net::saveConfig(c)) return sendError(500, "Could not save settings");

    sendJson(200, "{\"ok\":true,\"rebooting\":true}");
    wantReboot = true;
}

}  // namespace

bool begin() {
    if (!LittleFS.begin(false)) {
        log("[web] LittleFS mount failed — run 'pio run -t uploadfs'");
        // Still serve the API so the device is configurable without a UI.
    }

    server.on("/api/status",    HTTP_GET,  handleStatus);
    server.on("/api/list",      HTTP_GET,  handleList);
    server.on("/api/download",  HTTP_GET,  handleDownload);
    server.on("/api/delete",    HTTP_POST, handleDelete);
    server.on("/api/mkdir",     HTTP_POST, handleMkdir);
    server.on("/api/rename",    HTTP_POST, handleRename);
    server.on("/api/eject",     HTTP_POST, handleEject);
    server.on("/api/wifi/scan", HTTP_GET,  handleWifiScan);
    server.on("/api/wifi",      HTTP_POST, handleWifiSave);
    server.on("/api/upload",    HTTP_POST, handleUploadDone, handleUploadData);

    server.onNotFound(handleNotFound);
    server.begin();
    log("[web] listening on port 80");
    return true;
}

void loop() { server.handleClient(); }
bool rebootRequested() { return wantReboot; }

}  // namespace web
