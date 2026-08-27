#include "ws.h"
#include <WebSocketsServer.h>

namespace ws {
namespace {

WebSocketsServer* server = nullptr;

void onEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
    (void)num; (void)payload; (void)len;
    if (type == WStype_CONNECTED) {
        // Push current status on connect (caller should have called broadcastStatus recently)
    }
}

}  // namespace

void begin() {
    server = new WebSocketsServer(81);
    server->begin();
    server->onEvent(onEvent);
}

void loop() {
    if (server) server->loop();
}

void broadcast(const String& json) {
    if (!server) return;
    String copy = json;
    server->broadcastTXT(copy);
}

void broadcastProgress(const String& name, uint8_t pct, uint32_t rateBps, uint32_t etaSec) {
    String j = "{\"type\":\"progress\",\"name\":\"";
    // Minimal escape for name
    for (size_t i=0;i<name.length();++i){ char c=name[i]; if(c=='"') j+="\\\""; else if(c=='\\') j+="\\\\"; else j+=c; }
    j += "\",\"pct\":" + String(pct);
    j += ",\"rate\":" + String(rateBps);
    j += ",\"eta\":" + String(etaSec) + "}";
    broadcast(j);
}

void broadcastStatus(const String& statusJson) {
    // Wrap status so client can distinguish
    String j = "{\"type\":\"status\",\"data\":";
    j += statusJson;
    j += "}";
    broadcast(j);
}

bool hasClients() {
    return server && server->connectedClients() > 0;
}

}  // namespace ws
