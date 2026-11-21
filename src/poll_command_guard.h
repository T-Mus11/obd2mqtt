#pragma once
// Small, Arduino-independent guard for the opt-in MQTT on-demand poll API.
// No allocations, secrets, retained state or new tasks.
#include <stddef.h>
#include <stdint.h>

namespace PollCommandGuard {

inline bool validRequestId(const char* id, size_t len) {
  if (!id || len == 0 || len > 48) return false;
  for (size_t i = 0; i < len; ++i) {
    const char c = id[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
  }
  return true;
}

inline bool freshTimestamp(int64_t now, int64_t issued) {
  // Unix seconds: reject unsynchronised clocks, stale broker deliveries and future commands.
  return now >= 1700000000LL && issued > 0 &&
         issued <= now + 5 && issued >= now - 20;
}

}  // namespace PollCommandGuard
