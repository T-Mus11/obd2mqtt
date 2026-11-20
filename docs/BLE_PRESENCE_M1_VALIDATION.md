# BLE Presence M1 — testy odbiorowe i znane ograniczenia

**Status:** WIP / testy jednostkowe hosta zaliczone; kompilacja PlatformIO i sprzęt niezweryfikowane. Implementacja tylko na gałęzi `feat/ble-presence-m1`. Nie scalać ani nie wgrywać bez testów i osobnej decyzji użytkownika.
**Baza:** `main` @ `f830e845c3bb8db1926860fb2d2448cf3e316a82` (v0.3.24). **Wymagania:** `docs/BLE_PRESENCE_BACKLOG.md` z Draft PR #1 (niescalony).

## Co faktycznie powstało

- Moduł `BlePresence`: do 8 urządzeń ze stałym MAC i logicznym ID.
- Nieblokujące okna BLE 4000 ms co 30000 ms, jeden skaner NimBLE; dotychczasowe ręczne skanowanie zachowane.
- Stany `UNKNOWN`, `IN_RANGE`, `NOT_IN_RANGE`; brak reklam w trzech **zakończonych** oknach jest warunkiem NOT_IN_RANGE. Przerwane okno nie zwiększa licznika. Po 120 s bez poprawnego zakończenia jakiegokolwiek okna skanowania stan przechodzi do `UNKNOWN`, bez zdarzenia sugerującego ruch pojazdu.
- MQTT: retained `<base>/ble/<id>/state` i `<base>/ble/<id>/rssi`, `<base>/ble/<id>/last_seen_epoch` po poprawnym pomiarze czasu; eventy `<base>/ble/events` **non-retained** (tylko przejścia IN_RANGE/NOT_IN_RANGE po uzyskaniu stanu początkowego).
- Osobne encje HA MQTT Discovery dla stanu i RSSI każdego ID; wspólne urządzenie ESP32. Niewielka lista ogłoszonych ID jest zapamiętywana w LittleFS i umożliwia czyszczenie usuniętych discovery oraz retained state/RSSI/timestamp po restarcie (do potwierdzenia w integracji).
- Brak automatycznych odczytów PID/ECU: te należą do późniejszego kontraktu `poll_data(adapter_id)`.

## Konfiguracja — przykład (nie zawiera sekretów)

Nowe klucze `/config.json` lub częściowego JSON wysłanego przez istniejące `POST /api/config`:

```json
{
  "ble_presence_enabled": true,
  "ble_presence_devices": [
    {"id": "car_a", "name": "Car A", "mac": "AA:BB:CC:11:22:33"},
    {"id": "car_b", "name": "Car B", "mac": "AA:BB:CC:44:55:66"}
  ]
}
```

Adresy MAC w przykładzie są **fikcyjne**. Walidacja odrzuca niepoprawne ID/MAC, duplikaty ID/MAC, a maksymalnie zapisuje osiem pozycji. Nie ma jeszcze rozbudowanego edytora tej listy. Domyślnie `ble_presence_enabled=false`.

**Uwaga:** istniejąca v0.3.24 nadal konfiguruje tylko **jeden** adapter OBD i profil PID. Rejestr BLE nie zmienia jeszcze tej części kodu; drugi adapter jest w M1 tylko obserwowany.

## Testy wymagane przed uznaniem PR za gotowy

| ID | Scenariusz | Oczekiwany wynik |
|---|---|---|
| T01 | Build PlatformIO `esp32dev` (oraz zadeklarowane targety `esp32s3`, `esp32c3`) | Wszystkie wspierane buildy przechodzą; brak błędów API NimBLE |
| T02 | Uruchomienie z `ble_presence_enabled=false` | Brak nowych sesji BLE, zachowanie legacy Poll now/AT/MQTT bez regresji |
| T03 | Dwa adaptery BLE + niezależny tag | Każdy ma własne stabilne ID/stan/RSSI; nic nie jest mapowane na błędny ID |
| T04 | Uruchomienie lub restart ESP32 | Początkowy UNKNOWN; brak fałszywego eventu przyjazdu |
| T05 | Brak reklam przez 1–2 pełne okna | Nie przechodzi jeszcze do NOT_IN_RANGE |
| T06 | Brak reklam przez 3 pełne okna | Przechodzi w NOT_IN_RANGE, publikuje pojedyncze zdarzenie |
| T07 | Adapter wraca do zasięgu | IN_RANGE oraz pojedyncze przejście NOT_IN_RANGE → IN_RANGE |
| T08 | Manualne „Scan for dongles”, trwające odpytywanie ELM, sesja AT | Brak fałszywego NOT_IN_RANGE podczas pauzy; stabilność poprzednich funkcji |
| T09 | Zanik i powrót Wi-Fi/MQTT, restart HA | Odtwarzanie Discovery i stanów; niedostępność ESP nie oznacza NOT_IN_RANGE |
| T10 | Edycja/usunięcie konfiguracji BLE, restart urządzenia | Bez sierot MQTT Discovery; nie są emitowane stare stany |
| T11 | Minimum 1 h ciągłej obserwacji oraz 5 i 8 tagów | Stabilność heap, watchdog, Wi-Fi, BLE i czasu pętli; brak restartów |
| T12 | Vgate: brak zapłonu 60 min, otwarcie drzwi bez kluczyka | BLE może wrócić, ECU nadal NO DATA; żaden odczyt PID nie jest inicjowany samoczynnie |
| T13 | Wariant niski RSSI i krótkie zaniki | Histereza na poprawnie zakończonych skanach; brak fałszywej interpretacji ruchu |
| T14 | Połączenie z OBD i pomiar wpływu na akumulator | Nie pogarsza istotnie stanu spoczynku w stosunku do v0.3.24 (progi po pomiarach) |

## Znane ograniczenia i ryzyka M1

1. **Brak zweryfikowanej kompilacji i testów hardware.** Workflow GitHub Actions `.github/workflows/platformio-build.yml` został dodany, ale nie potwierdzono jego uruchomienia. Testy na hoście (`g++` C++11) dla samego automatu stanów zakończyły się powodzeniem; nie dowodzi to kompilacji firmware.
2. Obecny odczyt RSSI jest próbką z listy wyników skanowania, a nie pełnym strumieniem reklam. Brak trendów i ich heurystyk po stronie ESP.
3. Nieregularne reklamy i skanowanie 4 s / 30 s mogą wymagać kalibracji. Nieobecność reklamy nie dowodzi odjazdu; `NOT_IN_RANGE` to tylko niewykrywalność.
4. Urządzenia z rotującym MAC i bez stabilnego identyfikatora reklam nie są obsługiwane.
5. **Usuwanie discovery po zimnym restarcie** otrzymało trwały indeks `/ble_ha_discovery_ids.json` w LittleFS. Nadal wymagany jest test na HA z usunięciem urządzenia, utratą MQTT i restartem ESP32; zmiany `mqttBase` lub prefiksu Discovery mogą wymagać osobnej procedury migracji.
6. Niezmierzone obciążenie BLE/Wi-Fi/heap. Trzeba sprawdzić wpływ na istniejący Poller oraz jego watchdog.
7. Rejestr BLE nie daje jeszcze interfejsu poleceń HA, wielu profili ELM ani funkcji `poll_data(adapter_id)`. Następny etap M2.

## Rollback

Nie wgrywać na produkcję w fazie WIP. Przed próbą na urządzeniu testowym zachować aktualny firmware v0.3.24, kopię konfiguracji/profili (bez commitowania sekretów) i wynik odczytu statusu. W przypadku błędu powrócić do uprzednio działającego firmware, z wyłączoną nową funkcją. Żadnych automatycznych aktualizacji/flashowania z CI.

## Raport na dzień opracowania

- Odczytano aktualne pliki gałęzi `main`, w tym `README.md`, `platformio.ini`, `src/elm_ble.cpp`, `src/poller.cpp`, `src/mqtt_ha.cpp` i konfigurację. W katalogu głównym brak `AGENTS.md`.
- Dodano kod M1 i kontrolny workflow na osobnej gałęzi, Draft PR #2.
- Wprowadzono wydzielony `src/ble_presence_state.h` i `test/native/ble_presence_state_test.cpp`; **lokalnie uruchomiono** `g++ -std=c++11 -Wall -Wextra -Werror -Isrc ... && /tmp/ble_presence_state_test` — **PASS**, wynik 0. To tylko test logiki stanów, nie firmware/NimBLE.
- Dodano test C++11 do workflow oraz poprawiono obsługę `UNKNOWN` przy nieaktualnym skanowaniu i trwałe czyszczenie MQTT Discovery.
- Brak dostępu do działającego środowiska PlatformIO i do bezpośredniego klonowania repo w kontenerze; do momentu przejścia GitHub Actions kompilacja jest niepotwierdzona.
- Nie wykonano testów sprzętu, OTA, merge ani wdrożenia.
