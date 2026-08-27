#include "auth.h"
#include <Preferences.h>
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>
#include "config.h"

namespace auth {
namespace {

Preferences prefs;
String      cachedUser;
String      cachedHash;
bool        required = true;

String toHex(const uint8_t* d, size_t n) {
    char out[65];
    for (size_t i = 0; i < n; ++i) sprintf(out + i * 2, "%02x", d[i]);
    out[n * 2] = 0;
    return String(out);
}

void load() {
    prefs.begin("printdrop", true);
    cachedUser = prefs.getString("auth_user", PRINTDROP_AUTH_USER);
    cachedHash = prefs.getString("auth_hash", PRINTDROP_AUTH_PASS_HASH);
    prefs.end();
    // If NVS was empty, seed it
    if (cachedUser.isEmpty() || cachedHash.isEmpty()) {
        cachedUser = PRINTDROP_AUTH_USER;
        cachedHash = PRINTDROP_AUTH_PASS_HASH;
        prefs.begin("printdrop", false);
        prefs.putString("auth_user", cachedUser);
        prefs.putString("auth_hash", cachedHash);
        prefs.end();
    }
#if PRINTDROP_AUTH_REQUIRED == 0
    required = false;
#else
    required = true;
#endif
    // Allow runtime override via NVS key auth_required
    prefs.begin("printdrop", true);
    if (prefs.isKey("auth_required")) required = prefs.getBool("auth_required", required);
    prefs.end();
}

}  // namespace

String sha256Hex(const String& plain) {
    uint8_t hash[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (const uint8_t*)plain.c_str(), plain.length());
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);
    return toHex(hash, 32);
}

void begin() { load(); }

bool isRequired() { return required; }

String currentUser() { return cachedUser; }

bool checkBasicAuth(const String& header) {
    if (!required) return true;
    if (header.isEmpty()) return false;
    // Header is "Basic <base64>"
    const String prefix = "Basic ";
    if (!header.startsWith(prefix)) return false;
    String b64 = header.substring(prefix.length());
    b64.trim();
    // Decode
    size_t outLen = 0;
    // First call to get length
    int ret = mbedtls_base64_decode(nullptr, 0, &outLen,
                                    (const uint8_t*)b64.c_str(), b64.length());
    if (ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && ret != 0) return false;
    String decoded;
    decoded.reserve(outLen + 1);
    uint8_t* buf = (uint8_t*)malloc(outLen + 1);
    if (!buf) return false;
    ret = mbedtls_base64_decode(buf, outLen, &outLen,
                                (const uint8_t*)b64.c_str(), b64.length());
    if (ret != 0) { free(buf); return false; }
    buf[outLen] = 0;
    decoded = String((char*)buf);
    free(buf);
    int colon = decoded.indexOf(':');
    if (colon < 0) return false;
    String user = decoded.substring(0, colon);
    String pass = decoded.substring(colon + 1);
    if (user != cachedUser) return false;
    String h = sha256Hex(pass);
    h.toLowerCase();
    String ch = cachedHash;
    ch.toLowerCase();
    return h == ch;
}

bool setCredentials(const String& user, const String& passPlain) {
    if (user.isEmpty() || passPlain.isEmpty()) return false;
    String h = sha256Hex(passPlain);
    h.toLowerCase();
    prefs.begin("printdrop", false);
    bool ok = prefs.putString("auth_user", user) && prefs.putString("auth_hash", h);
    prefs.end();
    if (ok) { cachedUser = user; cachedHash = h; }
    return ok;
}

bool clear() {
    prefs.begin("printdrop", false);
    bool ok = prefs.putString("auth_user", PRINTDROP_AUTH_USER) &&
              prefs.putString("auth_hash", PRINTDROP_AUTH_PASS_HASH) &&
              prefs.putBool("auth_required", PRINTDROP_AUTH_REQUIRED != 0);
    prefs.end();
    if (ok) load();
    return ok;
}

}  // namespace auth
