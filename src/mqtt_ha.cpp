#include "mqtt_ha.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <LittleFS.h>
#include "config.h"
#include "log.h"
#include "i18n.h"
#include "poller.h"
#include "poll_command_guard.h"
#include "obd_adapter_registry.h"
#include "ble_presence.h"

namespace MqttHa {

static WiFiClient net;
static PubSubClient mqtt(net);
static uint32_t lastTry = 0;
static bool discoveryPending = true;
static std::vector<String> announced;   // bereits per Discovery angelegte Sensor-IDs
static std::vector<String> announcedBle; // persisted to permit cleanup after restart
static const char* bleDiscoveryCache = "/ble_ha_discovery_ids.json";
static String lastPollRequestId;  // bounded duplicate guard; stale commands rejected by timestamp
static String pendingPollAckJson; // publish only AFTER PubSubClient::loop() callback returns
static String vehicleContext;     // only set while a non-default adapter is polled

static void loadAnnouncedBle() {
  File f = LittleFS.open(bleDiscoveryCache, "r");
  if (!f) return;
  JsonDocument d;
  if (!deserializeJson(d, f) && d.is<JsonArray>()) {
    for (JsonVariantConst id : d.as<JsonArrayConst>()) {
      if (!id.is<const char*>()) continue;
      String value = id.as<String>();
      if (validProfileName(value) && announcedBle.size() < 8) announcedBle.push_back(value);
    }
  }
  f.close();
}

static void saveAnnouncedBle(const std::vector<String>& next) {
  if (announcedBle == next) return; // avoid repeated flash writes on reconnect
  JsonDocument d;
  JsonArray arr = d.to<JsonArray>();
  for (const String& id : next) arr.add(id);
  File f = LittleFS.open(bleDiscoveryCache, "w");
  if (!f) return;
  serializeJson(d, f);
  f.close();
}

String deviceId() {
  uint8_t m[6];
  WiFi.macAddress(m);
  char b[20];
  snprintf(b, sizeof(b), "obd2mqtt_%02x%02x%02x", m[3], m[4], m[5]);
  return b;
}

static String topic(const String& sub) { return cfg.mqttBase + "/" + sub; }
static String telemetryTopic(const String& sub) {
  return topic(vehicleContext.length() ? "vehicles/" + vehicleContext + "/" + sub : sub);
}
void setVehicleContext(const String& adapterId) { vehicleContext = adapterId; }

static void addDevice(JsonDocument& d) {
  JsonObject dev = d["device"].to<JsonObject>();
  dev["identifiers"][0] = deviceId();
  dev["name"] = cfg.deviceName;
  dev["model"] = profile.model.length() ? profile.model : String("OBD2 BLE Bridge");
  dev["manufacturer"] = "OBD2MQTT (ESP32)";
  dev["sw_version"] = FW_VERSION;
  dev["configuration_url"] = "http://" + WiFi.localIP().toString() + "/";
  JsonArray av = d["availability"].to<JsonArray>();
  av.add<JsonObject>()["topic"] = topic("status");
}

static bool publishConfig(const String& component, const String& objId, JsonDocument& d) {
  String t = cfg.haPrefix + "/" + component + "/" + deviceId() + "/" + objId + "/config";
  String payload;
  if (!d.isNull()) {
    d["unique_id"] = deviceId() + "_" + objId;
    addDevice(d);
    serializeJson(d, payload);
  }
  return mqtt.publish(t.c_str(), payload.c_str(), true);   // leerer Payload = Entität entfernen
}

// <base>/profile = Anzeigename, <base>/profile/attributes = Details (JSON)
void publishProfile() {
  if (!mqtt.connected()) return;
  mqtt.publish(topic("profile").c_str(), (profile.name.length() ? profile.name : cfg.profile).c_str(), true);
  JsonDocument a;
  a["id"] = cfg.profile;
  a["name"] = profile.name;
  a["model"] = profile.model;
  if (profile.batteryKwh > 0) a["battery_kwh"] = profile.batteryKwh;
  if (profile.consumption > 0) a["consumption_kwh_100km"] = profile.consumption;
  JsonArray en = a["active_pids"].to<JsonArray>();
  for (auto& p : profile.pids) if (p.enabled) en.add(p.id);
  a["firmware"] = FW_VERSION;
  String s;
  serializeJson(a, s);
  mqtt.publish(topic("profile/attributes").c_str(), s.c_str(), true);
}

static void sendDiscovery() {
  std::vector<String> now;
  for (auto& p : profile.pids) {
    JsonDocument d;
    if (p.enabled) {
      d["name"] = p.name;
      d["state_topic"] = topic(p.id);
      if (p.unit.length()) d["unit_of_measurement"] = p.unit;
      if (p.deviceClass.length()) d["device_class"] = p.deviceClass;
      if (p.stateClass.length()) d["state_class"] = p.stateClass;
      if (p.icon.length()) d["icon"] = p.icon;
      d["suggested_display_precision"] = p.precision;
      now.push_back(p.id);
    }
    publishConfig("sensor", p.id, d);
  }
  // Entfernte PIDs aus HA löschen
  for (auto& old : announced) {
    bool still = false;
    for (auto& n : now) if (n == old) still = true;
    bool inProfile = false;
    for (auto& p : profile.pids) if (p.id == old) inProfile = true;
    if (!still && !inProfile) { JsonDocument empty; publishConfig("sensor", old, empty); }
  }
  announced = now;

  { // 12V-Bordnetz (vom Dongle gemessen)
    JsonDocument d;
    d["name"] = T("12V Bordnetz", "12V battery");
    d["state_topic"] = topic("voltage_12v");
    d["unit_of_measurement"] = "V";
    d["device_class"] = "voltage";
    d["state_class"] = "measurement";
    d["suggested_display_precision"] = 1;
    publishConfig("sensor", "voltage_12v", d);
  }
  { // Dongle erreichbar
    JsonDocument d;
    d["name"] = T("OBD-Dongle verbunden", "OBD dongle connected");
    d["state_topic"] = topic("car");
    d["payload_on"] = "online";
    d["payload_off"] = "offline";
    d["device_class"] = "connectivity";
    publishConfig("binary_sensor", "car", d);
  }
  if (cfg.sleepVoltage > 0) { // Auto wach (12V über Schwelle = DC/DC-Wandler aktiv)
    JsonDocument d;
    d["name"] = T("Auto wach", "Car awake");
    d["state_topic"] = topic("car_awake");
    d["icon"] = "mdi:car-electric";
    publishConfig("binary_sensor", "car_awake", d);
  } else { JsonDocument empty; publishConfig("binary_sensor", "car_awake", empty); }
  if (cfg.lowBattVoltage > 0) { // 12V-Warnung
    JsonDocument d;
    d["name"] = T("12V niedrig", "12V low");
    d["state_topic"] = topic("battery_low");
    d["device_class"] = "battery";   // ON = niedrig
    publishConfig("binary_sensor", "battery_low", d);
  } else { JsonDocument empty; publishConfig("binary_sensor", "battery_low", empty); }
  { // Hauptschalter Abfrage
    JsonDocument d;
    d["name"] = T("Abfrage", "Polling");
    d["state_topic"] = topic("polling");
    d["command_topic"] = topic("polling/set");
    d["payload_on"] = "ON";
    d["payload_off"] = "OFF";
    d["icon"] = "mdi:car-connected";
    d["entity_category"] = "config";
    publishConfig("switch", "polling", d);
  }
  mqtt.publish(topic("polling").c_str(), cfg.pollEnabled ? "ON" : "OFF", true);
  { // BLE-Signal
    JsonDocument d;
    d["name"] = "BLE Signal";
    d["state_topic"] = topic("ble_rssi");
    d["unit_of_measurement"] = "dBm";
    d["device_class"] = "signal_strength";
    d["entity_category"] = "diagnostic";
    publishConfig("sensor", "ble_rssi", d);
  }
  { // Letzter Fehler – aktuellen Stand sofort veröffentlichen (überschreibt alten retained Wert)
    String le = Poller::lastErrorText();
    mqtt.publish(topic("last_error").c_str(), le.length() ? le.c_str() : T("keiner", "none"), true);
  }
  { // Letzter Fehler
    JsonDocument d;
    d["name"] = T("Letzter Fehler", "Last error");
    d["state_topic"] = topic("last_error");
    d["entity_category"] = "diagnostic";
    d["icon"] = "mdi:alert-circle-outline";
    publishConfig("sensor", "last_error", d);
  }
  { // Aktives Fahrzeugprofil (Name als Wert, Details als Attribute)
    JsonDocument d;
    d["name"] = T("Fahrzeugprofil", "Vehicle profile");
    d["state_topic"] = topic("profile");
    d["json_attributes_topic"] = topic("profile/attributes");
    d["entity_category"] = "diagnostic";
    d["icon"] = "mdi:car-info";
    publishConfig("sensor", "profile", d);
  }
  // BLE Presence: tri-state sensor avoids mistaking UNKNOWN for NOT_IN_RANGE.
  // One bridge device, independent entities per configured observed BLE device.
  std::vector<String> currentBle;
  bool bleDiscoveryOk = true;
  if (cfg.blePresenceEnabled) {
    for (const auto& b : cfg.blePresenceDevices) {
      currentBle.push_back(b.id);
      JsonDocument state;
      state["name"] = b.name + " BLE presence";
      state["state_topic"] = topic("ble/" + b.id + "/state");
      state["icon"] = "mdi:bluetooth";
      bleDiscoveryOk = publishConfig("sensor", "ble_" + b.id + "_state", state) && bleDiscoveryOk;
      JsonDocument rssi;
      rssi["name"] = b.name + " BLE RSSI";
      rssi["state_topic"] = topic("ble/" + b.id + "/rssi");
      rssi["unit_of_measurement"] = "dBm";
      rssi["device_class"] = "signal_strength";
      rssi["entity_category"] = "diagnostic";
      bleDiscoveryOk = publishConfig("sensor", "ble_" + b.id + "_rssi", rssi) && bleDiscoveryOk;
    }
  }
  for (const auto& old : announcedBle) {
    bool found = false;
    for (const auto& id : currentBle) if (id == old) { found = true; break; }
    if (!found) {
      JsonDocument empty1, empty2;
      bleDiscoveryOk = publishConfig("sensor", "ble_" + old + "_state", empty1) && bleDiscoveryOk;
      bleDiscoveryOk = publishConfig("sensor", "ble_" + old + "_rssi", empty2) && bleDiscoveryOk;
      // Retained data from removed IDs must not be mistaken for live observations.
      bleDiscoveryOk = mqtt.publish(topic("ble/" + old + "/state").c_str(), "", true) && bleDiscoveryOk;
      bleDiscoveryOk = mqtt.publish(topic("ble/" + old + "/rssi").c_str(), "", true) && bleDiscoveryOk;
      bleDiscoveryOk = mqtt.publish(topic("ble/" + old + "/last_seen_epoch").c_str(), "", true) && bleDiscoveryOk;
    }
  }
  if (bleDiscoveryOk) {
    saveAnnouncedBle(currentBle);
    announcedBle = currentBle;
  } else {
    logf("BLE discovery: MQTT publication failed, retained IDs left for retry after reconnect");
  }
  BlePresence::publishAll();
  { JsonDocument empty; publishConfig("update", "firmware", empty); }   // Update-Entität aus 0.3.23 wieder entfernen
  publishProfile();
  logf(T("MQTT Discovery gesendet (%u PIDs aktiv)", "MQTT discovery sent (%u PIDs active)"), (unsigned)now.size());
}

// M2a: restricted public on-demand command, NOT yet multi-adapter or multi-metric.
static bool validPollId(const String& id) {
  return PollCommandGuard::validRequestId(id.c_str(), id.length());
}

static void publishPollAck(const String& id, const String& status) {
  JsonDocument d;
  if (validPollId(id)) d["request_id"] = id;
  d["status"] = status;
  String out;
  serializeJson(d, out);
  // PubSubClient reuses the inbound packet buffer; do not publish from its callback.
  pendingPollAckJson = out;
}

static void onMessage(char* t, byte* payload, unsigned int len) {
  const String incomingTopic(t);
  if (incomingTopic == topic("command/poll_data")) {
    if (len == 0 || len > 384) { publishPollAck("", "invalid_size"); return; }
    JsonDocument d;
    if (deserializeJson(d, payload, len) || !d.is<JsonObject>()) {
      publishPollAck("", "invalid_json");
      return;
    }
    const String id = d["request_id"] | "";
    if (!validPollId(id)) { publishPollAck("", "invalid_request_id"); return; }
    // Explicit intent. M2b resolves adapter_id to a saved BLE + ECU profile mapping.
    // Only full-profile, one-shot queries are supported; no user-supplied AT commands.
    const String adapter = d["adapter_id"] | "";
    const String metrics = d["metrics"] | "";
    if ((adapter != "current" && !ObdAdapterRegistry::validId(adapter.c_str())) ||
        metrics != "full" || !d["force"].is<bool>() ||
        !d["force"].as<bool>()) { publishPollAck(id, "unsupported_scope"); return; }
    // Non-retained commands are required; an absolute time-to-live also rejects
    // old commands replayed at reconnect/reboot (PubSubClient does not expose retain).
    time_t now = time(nullptr);
    int64_t issued = d["issued_at"].as<int64_t>();
    if (!PollCommandGuard::freshTimestamp((int64_t)now, issued)) {
      publishPollAck(id, "expired_or_clock_unsynced");
      return;
    }
    if (lastPollRequestId == id) { publishPollAck(id, "duplicate"); return; }
    const String outcome = Poller::requestMqttPoll(id, adapter);
    if (outcome != "accepted") { publishPollAck(id, outcome); return; }
    lastPollRequestId = id;
    publishPollAck(id, "accepted");
    return;
  }
  String tp(t), v;
  for (unsigned i = 0; i < len && i < 16; i++) v += (char)payload[i];
  v.trim();
  v.toUpperCase();
  if (tp == topic("polling/set")) {
    bool on = v == "ON" || v == "1" || v == "TRUE";
    if (on || v == "OFF" || v == "0" || v == "FALSE") Poller::requestEnabled(on);
  }
}

void begin() {
  loadAnnouncedBle();
  mqtt.setCallback(onMessage);
  mqtt.setBufferSize(1536);
  mqtt.setKeepAlive(30);
}

bool connected() { return mqtt.connected(); }
void requestDiscovery() { discoveryPending = true; }

void loop() {
  if (cfg.mqttHost.isEmpty() || WiFi.status() != WL_CONNECTED) return;
  if (!mqtt.connected()) {
    if (millis() - lastTry < 10000 && lastTry) return;
    lastTry = millis();
    mqtt.setServer(cfg.mqttHost.c_str(), cfg.mqttPort);
    String will = topic("status");
    bool ok = mqtt.connect(deviceId().c_str(),
                           cfg.mqttUser.length() ? cfg.mqttUser.c_str() : nullptr,
                           cfg.mqttPass.length() ? cfg.mqttPass.c_str() : nullptr,
                           will.c_str(), 1, true, "offline");
    if (!ok) { logf(T("MQTT-Verbindung fehlgeschlagen (rc=%d)", "MQTT connection failed (rc=%d)"), mqtt.state()); return; }
    logf(T("MQTT verbunden mit %s:%u", "MQTT connected to %s:%u"), cfg.mqttHost.c_str(), cfg.mqttPort);
    mqtt.publish(will.c_str(), "online", true);
    mqtt.subscribe(topic("polling/set").c_str());
    mqtt.subscribe(topic("command/poll_data").c_str(), 1);
    discoveryPending = true;
  }
  if (discoveryPending) { discoveryPending = false; sendDiscovery(); }
  mqtt.loop();
  if (pendingPollAckJson.length() && mqtt.connected()) {
    String ack = pendingPollAckJson;
    pendingPollAckJson = "";
    mqtt.publish(topic("command/poll_data/ack").c_str(), ack.c_str(), false);
  }
}

void publishValue(const String& id, double v, int precision) {
  if (!mqtt.connected()) return;
  mqtt.publish(telemetryTopic(id).c_str(), String(v, precision).c_str(), true);
}

void publishText(const String& sub, const String& v, bool retain) {
  if (!mqtt.connected()) return;
  mqtt.publish(telemetryTopic(sub).c_str(), v.c_str(), retain);
}

}  // namespace MqttHa
