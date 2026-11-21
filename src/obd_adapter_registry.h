#pragma once
// Input validation for the bounded OBD adapter/vehicle registry.
// Portable C++11: reused by firmware and native CI without Arduino.
#include <stddef.h>
#include <string.h>

namespace ObdAdapterRegistry {

inline bool validId(const char* id) {
  if (!id) return false;
  size_t n = strlen(id);
  if (n == 0 || n > 32) return false;
  for (size_t i = 0; i < n; ++i) {
    const char c = id[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
          c == '_' || c == '-')) return false;
  }
  return true;
}

inline bool validMac(const char* mac) {
  if (!mac || strlen(mac) != 17) return false;
  for (size_t i = 0; i < 17; ++i) {
    char c = mac[i];
    if (i == 2 || i == 5 || i == 8 || i == 11 || i == 14) {
      if (c != ':') return false;
    } else if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
      return false;
    }
  }
  return true;
}

inline bool validProtocol(const char* protocol) {
  if (!protocol) return false;
  if (!strcmp(protocol, "profile") || !strcmp(protocol, "auto")) return true;
  return strlen(protocol) == 1 &&
    ((protocol[0] >= '0' && protocol[0] <= '9') ||
     protocol[0] == 'A' || protocol[0] == 'B' || protocol[0] == 'C');
}

inline bool validAddressType(const char* type) {
  return type && (!strcmp(type, "auto") || !strcmp(type, "public") ||
                  !strcmp(type, "random"));
}

} // namespace ObdAdapterRegistry
