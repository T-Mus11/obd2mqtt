#pragma once
#include <Arduino.h>

// Passive multi-device BLE telemetry. No OBD commands or HA scheduling here.
namespace BlePresence {
void loop();           // call from Arduino main loop after Poller::loop()
void publishAll();     // republish current retained states after MQTT reconnect
}  // namespace BlePresence
