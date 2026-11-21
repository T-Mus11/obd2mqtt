# M2b — przypisanie adaptera BLE do samochodu, profilu i protokołu ECU

**Stan:** Draft, kod do CI i review. Baza: M2a (PR #3), które bazuje na M1 (PR #2). Nie scalać ani nie wgrywać OTA bez osobnej zgody. Firmware na urządzeniu użytkownika pozostaje `0.3.24-m1-test`; tutaj kompilowany jest niewdrożony `0.3.24-m2b-test`.

## Zakres

- Rejestr `obd_adapters` (maksymalnie **4** wpisy) jest niezależny od rejestru BLE Presence (maksymalnie 8 obserwowanych tagów w M1).
- Każdy wpis łączy `adapter_id`, `vehicle_id`, BLE MAC, `profile_id` wskazujące istniejący `/profiles/<name>.json` i kod protokołu ELM. Dopuszczone są również pole `addr_type` (auto/public/random) oraz `enabled`.
- Jeden adapter jest używany naraz. Jawne `poll_data` z HA wybiera ID z rejestru i wykonuje **jeden** odczyt wszystkich aktywnych PID-ów przypisanego profilu. `adapter_id=current` zachowuje istniejącą funkcję odczytu skonfigurowanego adaptera i tematy MQTT.
- Przed zmianą celu ESP32 rozłącza ELM/BLE, ładuje wskazany profil bez zapisywania w miejsce legacy `cfg.profile`, resetuje stan PID i komend/nagłówka CAN oraz wykonuje inicjalizację wskazanym protokołem. Po sesji rozłącza i przywraca zapisany stan dotychczasowego profilu. Nie ma dwóch równoczesnych połączeń.
- Odczyty dodatkowego adaptera idą do `<mqtt_base>/vehicles/<adapter_id>/<pid>` i `<mqtt_base>/vehicles/<adapter_id>/state`, nie nadpisują istniejących encji legacy. Odpowiedź `<mqtt_base>/command/poll_data/result` zawiera `adapter_id`, `vehicle_id`, `profile_id`, `protocol`, liczbę poprawnych PID-ów, świeże wartości, status i czas sesji. Jeśli wynik JSON przekracza budżet pakietu, `values_omitted=true`; osobne topiki PID pozostają.
- Dane rejestru są trwale przechowywane w istniejącym `/config.json`. Błędną listę (niedozwolony protokół, niepoprawny MAC, powielony ID/MAC/vehicle, ponad 4 wpisy) odrzucamy bez kasowania poprzedniej listy. Brak istniejącego `profile_id` jest sygnalizowany przy `poll_data`.

## Przykład konfiguracji (wartości fikcyjne, bez sekretów)

Częściowa konfiguracja `POST /api/config` (nie wykonywać na działającym ESP bez przeglądu):

```json
{
  "obd_adapters": [
    {
      "id": "fiat",
      "vehicle_id": "fiat_tipo",
      "mac": "AA:BB:CC:11:22:33",
      "profile_id": "fiat_tipo",
      "protocol": "6",
      "addr_type": "auto",
      "enabled": true
    },
    {
      "id": "nissan",
      "vehicle_id": "nissan_qashqai",
      "mac": "AA:BB:CC:44:55:66",
      "profile_id": "qashqai",
      "protocol": "profile",
      "addr_type": "auto",
      "enabled": true
    }
  ]
}
```

Profil `fiat_tipo` / `qashqai` w tym przykładzie **musi istnieć** jako osobny, zweryfikowany plik JSON w LittleFS. Przykład nie dowodzi istnienia tych plików na urządzeniu. Stare `cfg.profile=ioniq5` ma w instalacji testowej nazwę wyświetlaną Nissan — nie usuwać ani nie przemianowywać go automatycznie.

**Protokół:**
- `"profile"` = dokładne komendy ATSP zawarte w `profile.init` (domyślna, najbezpieczniejsza kompatybilność).
- `"auto"` = jawne `ATSP0`, automatyczny wybór interfejsu ECU.
- `"6"` = `ATSP6` (CAN 11 bit / 500 kbps), `"7"` = `ATSP7` (CAN 29 bit / 500 kbps); możliwe również poprawne kody `0..9,A..C` urządzeń zgodnych z ELM.
- Przy override firmware zastępuje istniejącą komendę `ATSP...` w sekwencji inicjalizacji (lub dodaje ją, gdy profil jej nie zawiera), pozostawiając inne polecenia profilu. Nie umożliwia użytkownikowi arbitralnej komendy AT w polu protokołu.

## Przykład publicznego polecenia HA

```json
{
  "request_id": "ha_fiat_001",
  "adapter_id": "fiat",
  "metrics": "full",
  "force": true,
  "issued_at": 1795200000
}
```

To jedynie przykład; `issued_at` jest czasem Unix **generowanym na bieżąco**. Nie używać powyższego dosłownie. Polecenie jest non-retained i ma krótkie okno ważności jak w M2a. Aby zażądać domyślnej konfiguracji, użyj `adapter_id=current`.

**Nie zmienia się**: mechanizm bezpieczeństwa z M2a — `poll_enabled=false`, `minVoltage>=12.0V`, poprawny **świeży** `ATRV` z porównaniem z ustawionym progiem, pojedyncza aktywna sesja i brak wymuszenia po restarcie. Napięcie `11.8V` zaobserwowane 21.11 wymaga weryfikacji multimetrem przed testami ECU.

## Ograniczenia i bramki

1. Rejestr obsługuje do czterech adapterów, ale nie kolejkuje jednoczesnych zleceń. Przy `busy` drugie żądanie jest odrzucane; pełna kolejka/arbitraż i automatyczny trigger Vgate będą odrębnym krokiem M2/M3.
2. `metrics=full` odczytuje aktywne PID-y **konkretnego profilu**; nie ma jeszcze wyboru `mvp`, Mode 03/07 ani diagnostyki wielu modułów ECU. To również nie oznacza pomiaru rzeczywistego przebiegu licznika — `0131` to dystans od skasowania DTC.
3. Dla adapterów rejestrowych obowiązuje auto-detekcja BLE service/characteristics i brak PIN. Osobne niestandardowe UUID/PIN per adapter pozostają poza M2b; przy adapterze wymagającym PIN potrzebna kolejna iteracja. W trybie `current` zachowano dotychczasowe ustawienia serwisu/charakterystyk i PIN.
4. W HA nie utworzono jeszcze MQTT Discovery osobnych pojazdów. Konsumuj result JSON i topiki `vehicles/<id>/...` w testach, potem przygotuj Discovery z trwałymi unique_id, bez mieszania historycznych danych.
5. PubSubClient nie ujawnia flagi retained; istniejące krótkie TTL `issued_at` **nie eliminują w 100%** odtworzenia zatrzymanej komendy w ostatnich 20 s. To znany blocker przed produkcją.
6. Każdy adapter używa wspólnych globalnych progów bezpieczeństwa 12 V i timeoutów M2a. Parametry per vehicle, optymalizacja wydajności PID, dłuższe okna retry i deduplikacja wielu request_id nie są jeszcze zaimplementowane.
7. Testy CI muszą wykazać: `native-presence-state`, `poll-command-guard`, `obd-adapter-registry`, PlatformIO `esp32dev`, `esp32s3`, `esp32c3`; test na fizycznym ECU i sprawdzenie raportowania HA/rozłączania ELM pozostają osobną bramką.

## Kryteria testowe

- Boot i brak `obd_adapters`: stary `current`, profil, retained MQTT i Web UI działają bez zmian.
- Dwa wpisy z różnym MAC, protokołem i profilem; pierwsza i druga sesja nie mieszają wartości; po każdej powrót do globalnego profilu.
- Zła nazwa profilu / nieznany adapter / wyłączony wpis / podwójny ID / MAC, niepoprawny protokół / zbyt duża lista: jawna odmowa lub brak nadpisania rejestru.
- Błędy BLE/ECU, utrata MQTT, zapłon wyłączony, niskie i nieznane napięcie, restart podczas żądania: brak nieskończonego retry i brak nakładania sesji.
- Test retencji MQTT, zużycia RAM i WDT na ESP32-WROOM-32D; po QA dołączenie encji HA dla nowego adaptera.

**Zapis testów GitHub Actions:** podawać konkretne run ID oraz statusy po zakończeniu. PR Draft nie jest zgodą na OTA.
