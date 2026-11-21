// No Arduino dependencies: g++ -std=c++11 -Wall -Wextra -Werror -Isrc test/native/ble_presence_state_test.cpp -o /tmp/ble_presence_state_test
#include <cassert>
#include <cstdint>
#include "ble_presence_state.h"

using BlePresence::State;
using BlePresence::Tracker;

int main() {
  Tracker t;
  assert(t.state == State::Unknown);
  assert(!t.completeWindow(false, 1000));
  assert(!t.completeWindow(false, 31000));
  assert(t.completeWindow(false, 61000));
  assert(t.state == State::NotInRange);
  assert(!t.completeWindow(false, 91000));  // no repeated transition
  assert(t.completeWindow(true, 121000));
  assert(t.state == State::InRange);
  assert(t.misses == 0);
  assert(!t.completeWindow(false, 151000));
  assert(!t.completeWindow(false, 181000));
  assert(t.state == State::InRange); // brief radio loss is not absence
  assert(t.completeWindow(false, 211000));
  assert(t.state == State::NotInRange);

  // A scan paused by the ELM poller must NOT be counted as another missing window.
  assert(!t.markStale(300000, 120000));
  assert(t.markStale(331000, 120000));
  assert(t.state == State::Unknown);
  assert(!t.markStale(500000, 120000)); // stale only once
  assert(t.completeWindow(true, 501000));
  assert(t.state == State::InRange);

  // Clock wraparound: stale detection is based on uint32_t subtraction.
  Tracker wrap;
  assert(wrap.completeWindow(true, UINT32_MAX - 1000u));
  assert(!wrap.markStale(500u, 3000u));
  assert(wrap.markStale(4000u, 3000u));
  assert(wrap.state == State::Unknown);

  // No scan completed => UNKNOWN, even after arbitrarily long wait.
  Tracker none;
  assert(!none.markStale(300000u, 120000u));
  assert(none.state == State::Unknown);
  return 0;
}
