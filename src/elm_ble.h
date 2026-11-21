#pragma once
#include <Arduino.h>
#include <vector>

// BLE-Client für ELM327-kompatible OBD2-Dongles
namespace ElmBle {

struct ScanEntry { String mac, name; int rssi; bool addrRandom; bool likelyObd; };

void begin();
bool connect(String& err);          // nutzt cfg.bleMac / UUIDs (oder Auto-Erkennung)
void disconnect();
// Explicit, short-lived target for a manual multi-adapter MQTT session.
// Keeps legacy cfg.bleMac/UUID/PIN unchanged and disconnects on every switch.
void setSessionTarget(const String& mac, const String& addrType);
void clearSessionTarget();
extern volatile bool quiet;         // true = Verbinden/Trennen nicht loggen (Auto schläft, Prüfung jede Minute)
bool connected();
uint32_t dropCount();               // Zähler ungeplanter Verbindungsabbrüche
bool relaxedParams();               // true = nach wiederholtem 0x208 auf langsame Verbindungsparameter umgestellt
int  rssi();
uint16_t mtu();                     // ausgehandelte MTU (0 = nicht verbunden)
String detectedUuids();             // "svc / notify / write" nach erfolgreicher Verbindung
String deviceName();                // Bluetooth-Name des Dongles (GAP-Name bzw. aus dem Scan)
String dongleKind();                // grobe Typ-Erkennung anhand der UUIDs

// Sendet einen Befehl und wartet auf den ELM-Prompt '>'. Rückgabe: Rohantwort (ohne '>')
bool command(const String& cmd, String& response, uint32_t timeoutMs);

std::vector<ScanEntry> scan(uint32_t ms);   // existing manual blocking scan
// Async, bounded background scan. Results are consumed only on the Arduino loop task.
// A connection or manual scan cancels the active presence window (no false absence).
bool startPresenceScan(uint32_t ms);
bool isPresenceScanning();
bool takePresenceScanResults(std::vector<ScanEntry>& out);
void stopPresenceScan();

}  // namespace ElmBle
