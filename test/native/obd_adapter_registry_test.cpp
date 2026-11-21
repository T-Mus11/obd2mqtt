// g++ -std=c++11 -Wall -Wextra -Werror -Isrc test/native/obd_adapter_registry_test.cpp -o /tmp/adapter_registry_test
#include <cassert>
#include "obd_adapter_registry.h"

int main() {
  using namespace ObdAdapterRegistry;
  assert(validId("fiat_tipo"));
  assert(validId("nissan-qashqai"));
  assert(!validId(""));
  assert(!validId("Fiat"));
  assert(!validId("../escape"));
  assert(!validId("vehicle / bad"));
  assert(!validId(nullptr));
  assert(validMac("D2:E0:2F:8D:52:48"));
  assert(!validMac("D2:E0:2F:8D:52"));
  assert(!validMac("D2:E0:2F:8D:52:ZZ"));
  assert(!validMac("d2:e0:2f:8d:52:48"));
  assert(validProtocol("profile"));
  assert(validProtocol("auto"));
  assert(validProtocol("6")); // ISO 15765-4 CAN 11 bit / 500 kbit
  assert(validProtocol("7")); // CAN 29 bit / 500 kbit
  assert(validProtocol("A"));
  assert(!validProtocol(""));
  assert(!validProtocol("99"));
  assert(!validProtocol("ATSP7")); // config is a protocol code, not arbitrary AT
  assert(validAddressType("auto"));
  assert(validAddressType("random"));
  assert(!validAddressType("anything"));
  return 0;
}
