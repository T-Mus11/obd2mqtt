# M2c — konfiguracja rejestru adapterów przez lokalny interfejs ESP32

**Status:** Draft, PR zależny od M2b (PR #4), M2a (PR #3) i M1 (PR #2). Bez automatycznego OTA, merge lub restartu. Wymaga odrębnego wgrania obrazu `0.3.24-m2c-test`; zainstalowane M1/M2b nie zawierają zakładki.

## Decyzja architektoniczna

Konfiguracja *adapter BLE → samochód → profil PID → protokół ELM* znajduje się lokalnie na ESP32 i jest edytowana przez jego Web UI, bez DevTools/JavaScript Console. Home Assistant jest warstwą ręcznego wyzwalania odczytu `poll_data`, zapisu wyników i automatyzacji, nie musi przechowywać drugiej kopii rejestru. Jest to rozdzielenie zgodne z US-20; przyszła HA karta może być tylko wygodnym klientem API ESP.

## UI

- Zakładka **OBD-Adapter** obok Profil/Terminal; w językach interfejsu DE/EN.
- Dane odczytane z `GET /api/config` oraz istniejącej listy `GET /api/profiles`, w tym nazwy plików i opisy profili.
- Lista 0–4 wpisów; **dodaj/usuń**, edytuj `id`, `vehicle_id`, MAC, przypisany istniejący `profile_id`, protokół `profile/auto/ATSP0..9,A..C`, typ BLE `auto/public/random`, oraz włącz/wyłącz.
- Przycisk **BLE** w każdym wierszu uruchamia istniejący ograniczony skan i pozwala wybrać zobaczony MAC bez ręcznego przepisywania. Skan nie próbuje komunikować się z ECU.
- Walidacja przed zapisem: ID (max 32, małe a-z, 0-9, `_`/`-`), `current` zarezerwowane dla starego adaptera, format MAC, istniejący profil, duplikat ID/MAC/samochodu, maks. 4 wpisy.
- Zapis wysyła **tylko** `{obd_adapters:[...]}` do istniejącego `POST /api/config` bez zmian Wi-Fi, MQTT, sekretów i globalnego profilu. Backend M2b odrzuca nieprawidłowe listy transakcyjnie. UI potwierdza zapis dopiero po ponownym GET i porównaniu wszystkich pól, nie tylko po komunikacie HTTP 200.
- Dla firmware bez rejestru `obd_adapters` formularz pokazuje wymaganie aktualizacji i blokuje zapis.
- Zapis ustawień nie wywołuje `poll_data`, nie zmienia `poll_enabled` i nie restartuje ESP. Ręczne wyzwolenie diagnostyki pozostaje w HA.

## Uruchomienie po odrębnej zgodzie na aktualizację

1. W UI ESP32 → System → Backup pobierz aktualne ustawienia i profile. Przed OTA porównaj hash obrazu i zachowaj rollback.
2. Wgraj właściwy plik `obd2mqtt-v0.3.24-m2c-test-esp32dev-ota.bin` przez System → Firmware-Update, **nie ZIP**; po aktualizacji sprawdź `/api/status` i stabilność.
3. Wejdź w **OBD-Adapter** → **+ Adapter** → wpisz ID `nissan`, `vehicle_id=nissan_qashqai`, wybierz właściwy BLE MAC przyciskiem **BLE** albo ręcznie, wybierz aktualnie sprawdzony profil z listy, ustaw `protocol=profile` (bez niepotwierdzonego wymuszenia CAN), zaznacz aktywny → **Adapter speichern**. Sprawdź komunikat potwierdzający zapisu.
4. Opcjonalnie dodaj dalsze adaptery. Rejestr dotyczy **tego** ESP32 i adapterów znajdujących się w jego zasięgu. Fiat w Valencii wymaga konfiguracji tamtejszego ESP32.
5. Dopiero po sprawdzeniu zasilania oraz gotowości ECU możesz z HA wysłać polecenie `poll_data(adapter_id=nissan,metrics=full)`. Nie uruchamiaj intensywnego pollingu, gdy ostatni odczyt Vgate wskazuje 11,8 V.

## Znane ograniczenia / bezpieczeństwo

- Zmiana rejestru jest **konfiguracją**, nie implementacją lokalnego auto-triggera Vgate, 600 s retry ani MQTT Discovery per-auto. BLE Presence tagów to osobny rejestr i osobna funkcja.
- Odczyt pozostaje M2b: jednorazowy, cały aktywny profil; nie ma Mode 03/07 ani odrębnego wyboru PID z HA, brak kolejkowania wielu aktywnych zleceń.
- W M2b adaptery rejestrowe nie mają indywidualnych PIN ani niestandardowych UUID; UI nie obiecuje tych funkcji.
- Istniejące ryzyko retained MQTT replay (nawet w TTL 20 s) wymaga naprawy przed produkcją.
- **Testy wymagane:** CI: kompilacja trzech architektur PlatformIO, native BLE/Poll/Registry, syntax-check JS osadzonego w C++; na fizycznym urządzeniu: widoczność strony, dodaj/usuń/wyłącz, persystencja przez restart, skan BLE i wybór MAC, błędne dane, zmiana profilu po odczycie, zużycie heap, stabilność MQTT. CI nie sprawdza prawdziwego DOM/przeglądarki ani komunikacji z ECU.

## Pliki

- `src/web_ui.h` — nowa zakładka, formularz, skan i read-back weryfikujący zapis.
- `platformio.ini` — wersja `0.3.24-m2c-test`.
- `.github/workflows/platformio-build.yml` — kontrola składni JavaScript i osobny obraz OTA M2c.
- `docs/OBD_M2C_ADAPTER_UI.md` — niniejsza dokumentacja.

Brak zmian produkcyjnych, brak skopiowanych sekretów i brak aktualizacji firmware na urządzeniu.

## Wynik CI — 21.11.2026

- **Kod:** commit `0c16f1960205be2c6a358fab8062f784e28749ba`.
- [GitHub Actions run 44890018499](https://github.com/T-Mus11/obd2mqtt/actions/runs/44890018499): **completed / success** — `esp32dev`, `esp32s3`, `esp32c3`, testy natywne BLE/Poll/Registry/Web UI oraz build rollback.
- Test Web UI: wyodrębnienie JS z `src/web_ui.h`, `node --check`, `test/native/obd_adapter_ui_test.js` — PASS (walidacja ID/MAC/profili/duplikatów oraz porównanie zapisu).
- `esp32dev`: statyczny RAM 60 528 / 327 680 B (18,5%); flash 1 603 981 / 1 966 080 B (81,6%). To **nie** pomiar runtime heap.
- Ręczny pakiet OTA (nie factory): [obd2mqtt-m2c-esp32dev-ota](https://github.com/T-Mus11/obd2mqtt/actions/runs/44890018499/artifacts/14701224603). Zawiera obraz `obd2mqtt-v0.3.24-m2c-test-esp32dev-ota.bin`, sumę kontrolną i manifest źródła.

**Nieprzeprowadzone:** fizyczny test widoku ESP32 w Chrome, zapis i reboot, skan BLE, odbiór MQTT i odczyt ECU. PR pozostaje Draft; nie wykonywano OTA ani aktualizacji produkcji.
