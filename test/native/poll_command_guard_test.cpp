// g++ -std=c++11 -Wall -Wextra -Werror -Isrc test/native/poll_command_guard_test.cpp -o /tmp/poll_guard_test
#include <cassert>
#include <cstdint>
#include <cstring>
#include "poll_command_guard.h"

int main() {
  using PollCommandGuard::validRequestId;
  using PollCommandGuard::freshTimestamp;

  assert(validRequestId("ha_20261121_001", std::strlen("ha_20261121_001")));
  const char* uuid = "123e4567-e89b-12d3-a456-426614174000";
  assert(validRequestId(uuid, std::strlen(uuid)));
  assert(!validRequestId("", 0));
  assert(!validRequestId("space invalid", 13));
  assert(!validRequestId("../escape", 9));
  assert(!validRequestId("has.dot", 7));
  assert(!validRequestId("abc", 49));
  assert(!validRequestId(nullptr, 4));

  const int64_t now = 1795200000LL;
  assert(freshTimestamp(now, now));
  assert(freshTimestamp(now, now - 20));
  assert(freshTimestamp(now, now + 5));
  assert(!freshTimestamp(now, now - 21));
  assert(!freshTimestamp(now, now + 6));
  assert(!freshTimestamp(now, 0));
  assert(!freshTimestamp(0, now));
  assert(!freshTimestamp(1699999999LL, 1699999999LL));
  return 0;
}
