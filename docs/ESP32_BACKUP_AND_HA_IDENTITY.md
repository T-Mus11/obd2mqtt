# ESP32-WROOM-32D — backup, odtworzenie i stała tożsamość Home Assistant

**Status:** procedura przygotowana, ale nie wykonana na fizycznym urządzeniu. Dotyczy jedynego ESP32-WROOM-32D w instalacji; nie wykonywać żadnego flashowania przed uzyskaniem i weryfikacją kopii.  
**Źródło:** `main` @ `f830e845c3bb8db1926860fb2d2448cf3e316a82`, PR #2 `feat/ble-presence-m1`. Stan aktualnie zainstalowanego firmware i pamięci flash **nie został zweryfikowany**.

## Warstwa 1: kopia konfiguracji aplikacji przez WWW

1. Na aktualnie działającym urządzeniu, na zaufanej sieci lokalnej, pobrać `http://<IP_ESP>/api/backup?secrets=1` (jeśli interfejs zabezpieczony hasłem, wymagane uwierzytelnienie).
2. Sprawdzić, że JSON ma `type: "obd2mqtt-backup"`, `with_secrets: true`, blok `config` oraz obiekt `profiles` ze wszystkimi własnymi profilami. Nie publikować zawartości pliku.
3. Zachować zaszyfrowaną kopię poza GitHub. Eksport z `secrets=1` zawiera hasła Wi-Fi, MQTT i panelu. Przy nieszyfrowanym HTTP pobierać tylko przez zaufaną sieć.
4. Zapis konfiguracji i profili jest także dostępny przez `POST /api/restore?config=1&profiles=1`, ale **odtwarzanie** wykonywać wyłącznie w sytuacji awaryjnej, po sprawdzeniu zgodności formatu. Endpoint kończy się restartem.

**Weryfikacja z kodu:** `src/web.cpp` eksportuje ustawienia przez `configToJson(cd,!secrets)` oraz wszystkie profile z `listProfiles()`; import wczytuje konfigurację i profile. `src/config.cpp` zapisuje ustawienia do LittleFS (`/config.json`), a profile do `/profiles/*.json`.

## Warstwa 2: możliwość powrotu do v0.3.24 (bez pełnego zrzutu flash)

**Stan użytkownika na 2026-11-20:** ESP32-D0WD, 4 MB flash, port COM8 podczas odczytu. Pełny 4 MiB obraz `esp32-backup.bin` został złożony z czterech odczytów, ale pierwsze 64 KiB nie odpowiadają osobnemu wcześniejszemu odczytowi. Powodu rozbieżności nie zdiagnozowano. **Nie traktować obrazu jako bezpiecznego do przywracania.** Użytkownik zdecydował, że dalsze zrzuty flash nie są potrzebne.

Dla szybkiego rollback potrzebny jest obraz **aplikacji OTA** z poprzednio działającej wersji, a nie pełen obraz pamięci z bootloaderem i danymi. Nie udało się potwierdzić dostępności oryginalnego, identycznego pliku OTA aktualnie zainstalowanej wersji. Dodany w CI job `rollback-v0324` odtwarza OTA z **niezmiennego commita** `main@f830e845c3bb8db1926860fb2d2448cf3e316a82`. Ponieważ ten commit zawiera osierocony `src/online_update.cpp` (używa nieistniejących `cfg.updateCheck` i `ElmBle::shutdown()`), CI wyłącza ten jeden plik **wyłącznie w środowisku kompilacji**. Z tego względu obraz jest **kompilacją źródeł v0.3.24**, ale **nie potwierdzono identyczności binarnej z firmware na urządzeniu**.

Artefakt GitHub Actions `rollback-source-rebuild-v0.3.24-esp32dev` obejmuje aplikację OTA, SHA256SUMS i pochodzenie źródła. **Przed pierwszą aktualizacją pobrać go z udanego uruchomienia CI, zweryfikować sumę SHA-256 i zachować prywatnie, poza repo.** Alternatywnie użyć oryginalnego firmware OTA v0.3.24 po niezależnym potwierdzeniu jego autentyczności, zgodności ze sprzętem oraz formatu. Sama obecność artefaktu z GitHub nie jest jeszcze dowodem, że został zapisany na komputerze użytkownika.

Rollback przez panel WWW używa aplikacji OTA dla `esp32dev`, bez kasowania LittleFS/NVS. Jeśli panel/firmware nie startuje, konieczna może być odbudowa przez USB z pełnego, właściwego obrazu factory lub obrazu kompatybilnego z partycjami; **nie używać pliku OTA jako obrazu factory pod adresem 0x0**. Nie modyfikować flash bez osobnego planu i zgody.

## Normalne aktualizacje M1, M2, ...: OTA, nie pełny flash

- Po potwierdzeniu backupów pobrać zatwierdzony, skompilowany dla `esp32dev` obraz **OTA aplikacji** z kontrolowanego commita/CI i wgrać przez istniejący panel `System → Firmware update` (obecny `POST /api/update` używa `Update.begin(UPDATE_SIZE_UNKNOWN,U_FLASH)`). Nie używać `factory.bin` jako OTA.
- OTA zapisuje partycję aplikacji i zwykle zachowuje pozostałe dane LittleFS/NVS, dlatego istniejąca konfiguracja nie powinna się wyzerować; jednak wymaga sprawdzenia na tym urządzeniu, nie stanowi zamiennika kopii flash.
- Nigdy nie modyfikować układu partycji podczas zwykłego OTA, nie używać `erase-flash` i nie wgrywać obrazu dla `esp32s3`/`esp32c3` na klasyczny `esp32dev`.
- Przed każdą wersją zachować: aktualny backup JSON, działający poprzedni obraz OTA, commit nowej wersji i hash pliku. Po aktualizacji sprawdzić web UI, MQTT, PID-y i identyfikatory encji.
- **Zabezpieczenie M1 przygotowane:** `src/config.cpp` używa `LittleFS.begin(false)`, a `src/main.cpp` nie uruchamia migracji profili ani pollingu BLE po błędzie montowania. `GET /api/status` ujawnia `fs_mounted`. Po błędzie dane pozostają nietknięte; bez sprawnego LittleFS przywracanie JSON wymaga najpierw naprawy/prowizjonowania filesystemu (nie odbywa się automatycznie). Wymagany test na sprzęcie.

## Kontrakt zachowania obecnego HA — MUST NOT CHANGE

Kod w `src/mqtt_ha.cpp` tworzy identyfikator urządzenia z ostatnich 3 bajtów adresu MAC Wi-Fi. `unique_id` encji to `deviceId() + "_" + objId`, a ścieżka Discovery to `haPrefix/component/deviceId/objId/config`.

Dla **tego samego fizycznego ESP32** i aktualizacji OTA:
- zachować bez zmiany `cfg.mqttBase`, `cfg.haPrefix`, `deviceId()` / MAC, dotychczasowe ID wszystkich sensorów PID i istniejące `unique_id`;
- nie tworzyć drugiego urządzenia Home Assistant przez zmianę prefiksu MQTT lub schematu ID; **nie usuwać** starej integracji ani encji przed testem;
- nowe encje BLE dołączać addytywnie, pod nowymi stabilnymi identyfikatorami; ich pojawienie się nie wymaga ponownej integracji MQTT po stronie HA;
- nie zakładać, że przywrócenie pełnego flash na **innej** fizycznej płytce zachowa `deviceId()`, bo MAC fizyczny będzie inny;
- przed release wykonywać regresję: urządzenie MQTT pozostało jedno, istniejące encje mają te same `unique_id`, entity IDs / automatyzacje HA działają; nowe encje się pojawiają; usunięte encje są czyszczone jawnie;
- zmianę MQTT base, HA prefix lub struktury `unique_id` traktować jak **migrację wymagającą osobnego planu**, nie zwykły update.

## Potwierdzony stan backupu konfiguracji na 2026-11-20

Użytkownik sprawdził plik wyeksportowany z fizycznego ESP32: `type=obd2mqtt-backup`, `format=1`, `fw=0.3.24`, `with_secrets=true`, 4 profile, w tym Nissan. Aktywny w JSON nadal jest `ioniq5` — nie zmieniano tej wartości. Plik znajduje się po stronie użytkownika; asystent nie posiada kopii, nie widział jego zawartości ani nie testował odtwarzania. Plik zawiera sekrety.

## Bramka przed M1 na jedynym hardware

- [x] Użytkownik zweryfikował format JSON, obecność haseł i 4 profili, w tym Nissan
- [ ] Użytkownik zachował zaszyfrowaną kopię JSON poza repo i potwierdził jej lokalizację
- [ ] Odtworzone OTA v0.3.24 z niezmiennego źródła przeszło CI; artefakt z SHA256 zapisano poza GitHub
- [x] Wyeliminowano automatyczne formatowanie LittleFS i dodano jawny status montowania w kodzie M1 (bez testu na sprzęcie)
- [ ] Nowy obraz M1 `esp32dev` (wersja `0.3.24-m1-test`) z jednego commita przeszedł CI i ma zachowaną sumę kontrolną
- [ ] Przed OTA zanotowano rzeczywistą wartość `mqtt_base`, `ha_prefix`, wersję firmware oraz aktualne encje HA, bez ujawniania sekretów
- [ ] Przed OTA użytkownik osobno zatwierdził wgrywanie do fizycznego ESP32

**Bez zmian na fizycznym urządzeniu:** żadnej operacji OTA, erase/flash, restore, restart ani modyfikacji konfiguracji HA nie wykonano w ramach prac nad PR #2.
