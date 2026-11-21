#pragma once
#include <stdint.h>

namespace BlePresence {

// Pure, platform-independent state tracking; do not infer car arrival/departure.
enum class State : uint8_t { Unknown, InRange, NotInRange };

inline const char* stateText(State state) {
  switch (state) {
    case State::InRange: return "IN_RANGE";
    case State::NotInRange: return "NOT_IN_RANGE";
    default: return "UNKNOWN";
  }
}

struct Tracker {
  State state = State::Unknown;
  uint8_t misses = 0;
  uint32_t lastCompleteMs = 0;
  bool hasCompleted = false;

  // Call ONLY after a successfully completed scan window; aborted scans are ignored.
  bool completeWindow(bool seen, uint32_t now, uint8_t missingThreshold = 3) {
    hasCompleted = true;
    lastCompleteMs = now;
    State before = state;
    if (seen) {
      misses = 0;
      state = State::InRange;
    } else {
      if (missingThreshold == 0) missingThreshold = 1;
      if (misses < missingThreshold) ++misses;
      if (misses >= missingThreshold) state = State::NotInRange;
    }
    return before != state;
  }

  // Long periods without completed scans indicate an UNKNOWN observation state.
  // Unsigned arithmetic intentionally supports millis() wraparound.
  bool markStale(uint32_t now, uint32_t timeoutMs) {
    if (!hasCompleted || (uint32_t)(now - lastCompleteMs) < timeoutMs) return false;
    hasCompleted = false;
    misses = 0;
    State before = state;
    state = State::Unknown;
    return before != state;
  }
};
}  // namespace BlePresence
