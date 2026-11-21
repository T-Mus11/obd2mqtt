# M2d — natywne przyciski MQTT Discovery na urządzeniu Auto OBD

**Architektura:** w Home Assistant nie potrzebujemy osobnego skryptu, pomocników ani pakietu YAML do ręcznego odczytu. ESP32 ma lokalny rejestr adapterów, sam konstruuje komendę `requestMqttPoll` po otrzymaniu `PRESS` na temacie przycisku i automatycznie publikuje ostatni wynik.

Zmiana przygotowana na nowej gałęzi, stacked na PR #5 (M2c). **Bez OTA, merge, restartu i testu ECU.**

## Encje HA na istniejącym urządzeniu `Auto OBD`

Dla każdego włączonego adaptera np. `nissan`:
- **Button** `Read OBD nissan`: ręczny pojedynczy odczyt *wszystkich aktywnych PID-ów* przypisanego profilu. Jeden klik = jedna próba. Nie włącza automatycznego pollingu.
- **Sensor** `Last OBD read nissan`: ostatni wynik (`success`, `partial`, `no_ecu_response`, `busy_or_unsafe`, `invalid_profile`, itd.) i atrybuty JSON (czas, PID, błędy, `request_id`). Stan opisuje **ostatni** test, nie aktualną kondycję auta.
- Dotychczasowy **switch** `Auto polling (legacy)` (dawny `Polling`) pozostaje i nadal włącza **ciągłe** odpytywanie głównego adaptera. Podczas testowania przycisków pozostawić OFF.

**MQTT:** Discovery przez dotychczasowe `homeassistant/button/<device-id>/obd_nissan_poll/config` i `homeassistant/sensor/<device-id>/obd_nissan_poll_status/config`. Sterowanie na `<mqtt_base>/vehicles/nissan/poll/press/<session_nonce>`, payload `PRESS` non-retained. Po każdym nawiązaniu połączenia MQTT ESP32 losuje nowy nonce i publikuje nowy config Discovery. Dzięki temu **stary retained PRESS na poprzednim temacie nie jest ponownie zasubskrybowany**. To ogranicza ryzyko retained replay natywnego przycisku; istniejące publiczne `<mqtt_base>/command/poll_data` z M2a zachowuje własne ograniczenie PubSubClient/TTL. Wymagane prawo HA do publikowania pod bieżącym nonce.

Wynik zapisywany (retained) w:
- `<mqtt_base>/vehicles/nissan/poll_status` — stan;
- `<mqtt_base>/vehicles/nissan/poll_result` — JSON atrybutów.

MQTT Discovery per-car PID/odometer i Mode 03/07 nadal nie są wdrożone. Istniejące PID-y legacy pozostają w głównym urządzeniu, a dodatkowe sesje trzymają PIDs w scoped topics `vehicles/<adapter_id>/<pid>`.

## Niewiarygodny pomiar ATRV — opt-in dla konkretnego adaptera

W rejestrze dodano `voltage_mode`: `guard` (**domyślny**, dotychczasowa ochrona) lub `ignore`.

Dla Nissana z Vgate, którego wskazanie zaniżało wg użytkownika około 1 V (a na produkcji zanotowano m.in. 11 V), ustaw w **ESP32 → OBD-Adapter → Voltage / ATRV → Ignore (no ATRV)** i zapisz.

Wtedy przy sesji tego **rejestrowego** adaptera:
- firmware **nie wysyła ATRV**; nie używa tego napięcia do decyzji o odpytywaniu, uśpieniu ani alertach,
- nie publikuje `voltage_12v` w scoped wynikach; nie myli wartości z innymi pojazdami,
- dopuszcza jedno ręczne żądanie pomimo globalnego `min_voltage = 0`, nadal wymaga `poll_enabled = false` i odrzuca duplikaty/równoległe joby/safe mode,
- stary adapter `current` i inne adaptery z `voltage_mode=guard` zachowują dotychczasową ochronę i minimalną konfigurację 12 V.

**Ważne:** to celowe usunięcie jednego zabezpieczenia na podstawie uznanej za wadliwą telemetrii, a **nie** dowód bezpiecznego napięcia pojazdu. Nie dodawaj automatycznych retry i nie włączaj legacy Polling na tym adapterze; odczyt jest świadomą akcją człowieka po niezależnym potwierdzeniu stanu akumulatora.

## Kroki po osobnej zgodzie na test OTA

1. Odczytaj backup ustawień i profili ESP32; zachowaj rollback.
2. Wgraj `obd2mqtt-v0.3.24-m2d-test-esp32dev-ota.bin` przez **System → Firmware update**. Sprawdź `/api/status`: `fw=0.3.24-m2d-test`. Nie wgrywaj ZIP/factory image.
3. W zakładce **OBD-Adapter** wpis `nissan`, `vehicle_id=nissan_qashqai`, poprawny MAC oraz profil `ioniq5`; ustaw **Voltage / ATRV → Ignore**, sprawdź zapis po Reload.
4. W **Status** pozostaw automatyczną `Polling` OFF.
5. W HA Polska: **Settings → Devices & services → MQTT → Auto OBD**. Poczekaj na Discovery po połączeniu ESP32. Powinny pojawić się przycisk **Read OBD nissan** i sensor **Last OBD read nissan**. Nie twórz skryptu HA. Jeśli stary pakiet `obd_nissan_mqtt.yaml` został już wdrożony, należy go usunąć zgodnie z procedurą i po sprawdzeniu konfliktów, ale **nie usuwać automatycznie**. Draft PR HA Polska #3 pozostaje nieaktywowany.
6. Test pierwszego przycisku wykonaj tylko świadomie, na bezpiecznie zaparkowanym samochodzie i po niezależnej kontroli akumulatora. Napięcie Vgate nie jest do tego wykorzystywane.

## Testy / bramki przed produkcją

- CI: PlatformIO esp32dev/esp32s3/esp32c3, natywne testy rejestru, UI i skryptów, kontrola kontraktu Discovery/nonce/no-ATRV.
- Test fizyczny: widoczność w HA, reconnect brokera i rotacja nonce bez replay, persistent status, skan MAC, `guard/ignore`, brak wysyłania ATRV na trybie ignore, bezpieczeństwo przy braku auta, reset ECU i odzyskiwanie, heap/WDT.
- Istniejące niewyjaśnione zachowanie Vgate przy pomiarze 12 V pozostaje faktem. Nie zakładamy stałego offsetu 1 V i nie implementujemy korekcji napięcia.
- Dla poprzednio utworzonego prywatnego PR #3 w `home-assistant-polska` nowa ścieżka **zastępuje pomysł odrębnego skryptu**, ale nie wykonuje zmian w tamtym repo ani w HA.
