#pragma once
#include <Arduino.h>

// Web UI auth: user + SHA-256 hex stored in NVS ("printdrop" auth_user/auth_hash).
// Build flags PRINTDROP_AUTH_USER/PASS_HASH seed a fresh device. Passwords are
// never stored plaintext. Use `echo -n "pass" | sha256sum` to generate.
// Call auth::begin() at boot, auth::checkBasic(header) in HTTP handlers.
namespace auth {

void begin();

// Returns true if auth is disabled (open) or header auto-passes.
bool isRequired();

// Verify "Basic base64(user:pass)" header. Empty header -> false when required.
bool checkBasicAuth(const String& header);

// Current user for display (not secret).
String currentUser();

// Set new credentials (hashes internally). Persists to NVS; caller should reboot
// or inform UI. Returns false on NVS error.
bool setCredentials(const String& user, const String& passPlain);

// Clear to defaults (admin / printdrop hash). Used by factory reset.
bool clear();

// Hash helper: SHA-256 hex (64 chars, lower).
String sha256Hex(const String& plain);

}  // namespace auth
