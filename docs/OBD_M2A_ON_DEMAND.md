# M2a — manualne jednokrotne `poll_data` z Home Assistant (Draft)

**Zakres:** niewielki, bezpieczny krok po M1 BLE Presence. To **nie** jest implementacja pełnego M2/M3 z backlogu: bez kolejki wielu adapterów, automatycznego triggera Vgate, autonomicznych retry przez 600 s, DTC Mode 03/07 ani wybierania dowolnego zakresu PID. Wersja firmware pozostaje `0.3.24-m1-test`, ponieważ niczego jeszcze nie wydano ani nie wgrano na ESP32; w razie przyszłego OTA wymagane jest osobne oznaczenie builda, backup i regresja.

**Baza kodu:** PR #2 `feat/ble-presence-m1`. Publiczne polecenie używa istniejącej ścieżki Poller `Poll now` i odpytuje **raz** wszystkie aktywne PID-y **bieżącego skonfigurowanego profilu**. Ten kod jest przeznaczony wyłącznie do przeglądu i kompilacji; bez automatycznego wdrożenia.

## Kontrakt testowy MQTT

Publikacja z HA: `<mqtt_base>/command/poll_data`, QoS 1, **retain=false**, JSON (max. 384 B):

```json
{
  "request_id": "ha_20261121_001",
  "adapter_id": "current",
  "metrics": "full",
  "force": true,
  "issued_at": 1795200000
}
```

**Uwaga:** `issued_at` powyżej to **przykładowy** timestamp. W działającej automatyzacji musi być generowany aktualny czas Unix w sekundach; każde żądanie dostaje unikalny ID. Firmware odrzuca starsze niż 20 s, przyszłe o ponad 5 s i polecenia przy niezsynchronizowanym zegarze. Retained MQTT **nie jest czytany jako atrybut w PubSubClient**, więc sprawdzanie wieku ogranicza ryzyko odtworzenia starej komendy, ale nie jest kryptograficzną ani absolutną gwarancją niewykonania retained polecenia z ostatnich 20 s. Przed oznaczeniem API jako produkcyjnego rozstrzygnąć tę lukę (np. unikalny `boot_id` / krótki challenge na reconnect lub inny transport).

Przykład wyłącznie do późniejszego ręcznego testu w HA (nie wgrywany do instalacji):

```yaml
action: mqtt.publish
data:
  topic: obd2mqtt/command/poll_data
  qos: 1
  retain: false
  payload: >-
    {{ dict(
      request_id=now().strftime('ha_%Y%m%d_%H%M%S_%f'),
      adapter_id='current',
      metrics='full',
      force=true,
      issued_at=as_timestamp(now())|int
    ) | to_json }}
```

Zależnie od produkcyjnego `mqtt_base` należy wybrać właściwy topic bez publikowania sekretów. MQTT broker wymaga odpowiednio ograniczonych uprawnień do topiców komend.

Potwierdzenie: `<mqtt_base>/command/poll_data/ack` (non-retained):

- `accepted`: zapytanie przyjęte, **nie** oznacza poprawnego wyniku ECU;
- `invalid_size / invalid_json / invalid_request_id`: błędny format;
- `unsupported_scope`: M2a akceptuje **wyłącznie** `current`, `full` i jawne `force=true`; np. `mvp`, `fiat_tipo` i `nissan` nie są jeszcze obsługiwane;
- `expired_or_clock_unsynced`: wiek polecenia lub zegar;
- `duplicate`: ponowiony ostatnio zaakceptowany ID;
- `busy_or_unsafe`: inna sesja/job, safe mode, brak adresu adaptera, legacy auto-poll włączony lub brak skonfigurowanego minimalnego zabezpieczenia napięcia.

Wynik: `<mqtt_base>/command/poll_data/result` (non-retained) zawiera `request_id`, `adapter_id=current`, `metrics=full`, `status`, `duration_ms`, `pids_enabled`, `pids_fresh`, opcjonalnie napięcie i ostatni błąd. Status: `success` (wszystkie aktywne PID poprawne), `partial`, `no_ecu_response`, `blocked_low_voltage`. Odrębne, istniejące sensory PID publikują wartości, ale retained sensory mogą przechowywać stare dane — wynik M2a rozróżnia świeżość w bieżącej sesji. **M2a nie wykonuje Mode 03/07**, więc nie odpowiada jeszcze kompletowi MVP US-23.

## Ochrona zasobów i napięcia

- Polecenia MQTT są dobrowolnym odczytem `force=true` i mogą obudzić ECU; nie są wyzwalane przez dodatkowe tagi ani domyślnie aktywne.
- Nowy punkt wejścia **nie** zmienia dotychczasowego `poll_enabled`; jest dozwolony tylko gdy legacy autopoll jest wyłączony oraz `minVoltage >= 12.0 V`. Następnie oryginalny `pollDue()` mierzy `ATRV` i porównuje wynik z rzeczywistą skonfigurowaną wartością `minVoltage`, zanim zacznie zapytania ECU.
- Aktualna obserwacja z 21.11: `ATRV=11.8 V`, ostrzeżenie niskiego 12 V i `NO DATA` przy niepotwierdzonym zapłonie. **Nie uruchamiać testu nowego API ani długich prób OBD przy takim wskazaniu bez weryfikacji multimetrem i progu konfiguracyjnego.**
- Fizyczny jeden adapter / jeden profil; brak rozbudowanej kolejki i brak gwarancji izolacji wielu samochodów do czasu M2. Aby mieć niezawodne wywołanie na żądanie dla dwóch aut, trzeba implementować rejestr adapterów i przełączanie kontekstu ELM.
- Kod używa małych komunikatów, jednego aktywnego request ID i istniejących zapytań ECU (te same `header+cmd` odpytywane tylko raz na cykl). Brak nowego skanera, nowego tasku i gromadzenia długiej historii na ESP.

## Testy / bramki

1. CI: zbudować `esp32dev / esp32s3 / esp32c3` i zachować dotychczasowy test stanów BLE. **Do potwierdzenia z GitHub Actions po utworzeniu PR.**
2. Niepoprawny JSON, za długi payload, stary timestamp, błędna lista metryk, nieznany adapter, brak `force`, duplikat ID — odmowa bez wywołania Pollera.
3. `poll_enabled=false` i brak polecenia — identyczne zachowanie jak M1. `poll_enabled=true` — nowe API odrzuca, nie przełącza samoczynnie trybu.
4. Battery guard 11.8 V i brak zapłonu: brak odczytu ECU przy bezpiecznym progu; test wyłącznie po potwierdzeniu napięcia. Sprawdzić stan `blocked_low_voltage` i czy nie zmieniają się stare wartości PID.
5. Podczas zapłonu i bezpiecznego zasilania: `ack=accepted`, jeden cykl Pollera, `result` ze świeżymi polami, nadal ten sam `unique_id` HA i profil, żadnych nowych osieroconych encji.
6. Restart ESP, reconnect MQTT, retained command w ciągu TTL — znane zagrożenie przed produkcją, wymagające zamknięcia; regresja z watchdog/heap.
7. Dalszy etap: wywoływanie `poll_data(adapter_id, metrics=mvp)` z wyborem auta, lokalnymi retry 600 s i DTC Mode 03/07, bez mieszania PID-ów / profili.

**Stan:** Draft / ograniczony do M2a. Żadne zmiany produkcyjne, restart, OTA czy merge nie są autoryzowane przez ten dokument.
