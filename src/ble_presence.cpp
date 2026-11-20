#include "ble_presence.h"
#include <vector>
#include <time.h>
#include "config.h"
#include "elm_ble.h"
#include "log.h"
#include "mqtt_ha.h"
#include "poller.h"

// All state changes and MQTT writes run in the main Arduino loop task.
// NimBLE callbacks never touch this registry or the MQTT client.
namespace BlePresence {
namespace {
constexpr uint32_t kScanDurationMs = 4000;
constexpr uint32_t kScanPeriodMs = 30000;
constexpr uint8_t kMissedWindows = 3;

struct Observation {
  String id, mac, name;
  String state = "UNKNOWN";
  int rssi = 0;             // zero = not measured (never sent as a dBm sample)
  uint8_t misses = 0;
  uint32_t lastRssiPublish = 0;
};

std::vector<Observation> devices;
bool enabled = false;
uint32_t nextScanAt = 0;
uint32_t lastConfigCheck = 0;

void publishState(const Observation& d) {
  MqttHa::publishText("ble/" + d.id + "/state", d.state);
}

void publishRssi(const Observation& d) {
  MqttHa::publishText("ble/" + d.id + "/rssi",
                      d.state == "IN_RANGE" && d.rssi < 0 ? String(d.rssi) : String("unknown"));
}

void transition(Observation& d, const String& to, uint32_t now) {
  if (d.state == to) return;
  String from = d.state;
  d.state = to;
  publishState(d);
  publishRssi(d);
  // UNKNOWN after boot is not a physical movement event.
  if (from == "UNKNOWN" || to == "UNKNOWN") return;
  JsonDocument evt;
  evt["id"] = d.id;
  evt["from"] = from;
  evt["to"] = to;
  evt["uptime_ms"] = now;
  String json;
  serializeJson(evt, json);
  MqttHa::publishText("ble/events", json, false);
}

bool sameConfig() {
  if (enabled != cfg.blePresenceEnabled) return false;
  const size_t count = cfg.blePresenceEnabled ? cfg.blePresenceDevices.size() : 0;
  if (devices.size() != count) return false;
  for (size_t i = 0; i < count; ++i)
    if (devices[i].id != cfg.blePresenceDevices[i].id ||
        devices[i].mac != cfg.blePresenceDevices[i].mac ||
        devices[i].name != cfg.blePresenceDevices[i].name) return false;
  return true;
}

void syncConfig() {
  if (sameConfig()) return;
  ElmBle::stopPresenceScan();
  std::vector<Observation> updated;
  if (cfg.blePresenceEnabled) {
    updated.reserve(cfg.blePresenceDevices.size());
    for (const auto& d : cfg.blePresenceDevices) {
      Observation o;
      o.id = d.id;
      o.mac = d.mac;
      o.name = d.name;
      for (const auto& previous : devices) {
        if (previous.id == o.id && previous.mac == o.mac) {
          o = previous;
          o.name = d.name;
          break;
        }
      }
      updated.push_back(o);
    }
  }
  devices.swap(updated);
  enabled = cfg.blePresenceEnabled;
  nextScanAt = 0;
  MqttHa::requestDiscovery();
  publishAll();
}

void processWindow(const std::vector<ElmBle::ScanEntry>& seen, uint32_t now) {
  for (auto& d : devices) {
    const ElmBle::ScanEntry* match = nullptr;
    for (const auto& e : seen) {
      if (e.mac.equalsIgnoreCase(d.mac)) { match = &e; break; }
    }
    if (match) {
      d.misses = 0;
      int previousRssi = d.rssi;
      d.rssi = match->rssi;
      if (d.state != "IN_RANGE") transition(d, "IN_RANGE", now);
      else if (previousRssi != d.rssi &&
               (d.lastRssiPublish == 0 || now - d.lastRssiPublish >= 30000)) publishRssi(d);
      d.lastRssiPublish = now;
      if (timeValid()) MqttHa::publishText("ble/" + d.id + "/last_seen_epoch", String((uint32_t)time(nullptr)));
    } else {
      if (d.misses < kMissedWindows) ++d.misses;
      if (d.misses >= kMissedWindows) transition(d, "NOT_IN_RANGE", now);
    }
  }
}
}  // namespace

void publishAll() {
  if (!MqttHa::connected()) return;
  for (const auto& d : devices) {
    publishState(d);
    publishRssi(d);
  }
}

void loop() {
  uint32_t now = millis();
  if (lastConfigCheck == 0 || now - lastConfigCheck >= 1000) {
    lastConfigCheck = now;
    syncConfig();
  }
  if (!enabled || devices.empty() || Poller::isPaused()) {
    ElmBle::stopPresenceScan();
    return;
  }
  if (ElmBle::connected()) {
    ElmBle::stopPresenceScan();   // existing ELM connection has priority
    return;
  }
  std::vector<ElmBle::ScanEntry> result;
  if (ElmBle::takePresenceScanResults(result)) processWindow(result, now);
  if (ElmBle::isPresenceScanning()) return;
  if ((int32_t)(now - nextScanAt) < 0) return;
  nextScanAt = now + kScanPeriodMs;
  ElmBle::startPresenceScan(kScanDurationMs);
}
}  // namespace BlePresence
