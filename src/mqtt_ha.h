#pragma once
#include <Arduino.h>

namespace MqttHa {
void begin();
void loop();                       // Verbindung halten, Discovery nach (Re)Connect
bool connected();
String deviceId();
void requestDiscovery();           // z.B. nach Profiländerung
void publishValue(const String& id, double v, int precision);
void publishText(const String& sub, const String& v, bool retain = true);
// During a non-legacy adapter session, publish PID/status telemetry under
// <base>/vehicles/<adapter_id>/... to prevent cross-vehicle HA state corruption.
void setVehicleContext(const String& adapterId);
void publishProfile();             // <base>/profile + <base>/profile/attributes
}
