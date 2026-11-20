# ESP32-WROOM-32D — backup, odtworzenie i stała tożsamość Home Assistant

**Status:** procedura przygotowana, ale nie wykonana na fizycznym urządzeniu. Dotyczy jedynego ESP32-WROOM-32D w instalacji; nie wykonywać żadnego flashowania przed uzyskaniem i weryfikacją kopii.  
**Źródło:** `main` @ `f830e845c3bb8db1926860fb2d2448cf3e316a82`, PR #2 `feat/ble-presence-m1`. Stan aktualnie zainstalowanego firmware i pamięci flash **nie został zweryfikowany**.

## Warstwa 1: kopia konfiguracji aplikacji przez WWW

1. Na aktualnie działającym urządzeniu, na zaufanej sieci lokalnej, pobrać `http://<IP_ESP>/api/backup?secrets=1` (jeśli interfejs zabezpieczony hasłem, wymagane uwierzytelnienie).
2. Sprawdzić, że JSON ma `type: "obd2mqtt-backup"`, `with_secrets: true`, blok `config` oraz obiekt `profiles` ze wszystkimi własnymi profilami. Nie publikować zawartości pliku.
3. Zachować zaszyfrowaną kopię poza GitHub. Eksport z `secrets=1` zawiera hasła Wi-Fi, MQTT i panelu. Przy nieszyfrowanym HTTP pobierać tylko przez zaufaną sieć.
4. Zapis konfiguracji i profili jest także dostępny przez `POST /api/restore?config=1&profiles=1`, ale **odtwarzanie** wykonywać wyłącznie w sytuacji awaryjnej, po sprawdzeniu zgodności formatu. Endpoint kończy się restartem.

**Weryfikacja z kodu:** `src/web.cpp` eksportuje ustawienia przez `configToJson(cd,!secrets)` oraz wszystkie profile z `listProfiles()`; import wczytuje konfigurację i profile. `src/config.cpp` zapisuje ustawienia do LittleFS (`/config.json`), a profile do `/profiles/*.json`.

## Warstwa 2: pełny obraz aktualnej pamięci flash (USB, Windows)

Obraz flash zgrywa się **z aktualnie działającej płytki**, a nie pobiera z GitHub Releases: może zawierać inną wersję, niestandardowy układ partycji, aktywne profile, hasła i zmiany konfiguracji.

1. Przed odpięciem USB/zmianą połączeń zanotować obecny adres IP, nazwę MQTT, ID urządzenia i działające encje HA. Zamknąć Serial Monitor / inne aplikacje używające portu COM.
2. Podłączyć ESP32-WROOM-32D przewodem USB z transmisją danych; ustalić port COM w Windows. Nie zakładać, że numer COM z poprzedniego testu jest wciąż aktualny.
3. Zainstalować aktualny `esptool` w Pythonie: `py -m pip install --upgrade esptool`.
4. **Tylko odczyt:** sprawdzić chip i flash:
   ```powershell
   py -m esptool --chip esp32 --port COMx flash-id
   py -m esptool --chip esp32 --port COMx read-mac
   ```
5. **Tylko odczyt:** zrobić obraz o rozmiarze rozpoznanym automatycznie (esptool v5):
   ```powershell
   py -m esptool --chip esp32 --port COMx -b 460800 read-flash 0 ALL esp32-before-m1-full-flash.bin
   Get-Item .\esp32-before-m1-full-flash.bin | Select-Object Name,Length
   Get-FileHash .\esp32-before-m1-full-flash.bin -Algorithm SHA256
   ```
   Rozmiar pliku porównać z `flash-id` (dla 4 MB = 4 194 304 B). W razie niestabilnej transmisji zmniejszyć baud, np. do 115200.
6. Dla pewności wykonać **drugi odczyt** do pliku o innej nazwie i porównać SHA256. Dopiero wtedy kopię uznać za wiarygodną. Po odczycie może być konieczny reset urządzenia, aby opuścić tryb bootloadera.
7. Obraz flash może zawierać pełne sekrety i należy go przechowywać zaszyfrowanego i prywatnie, **nigdy w repozytorium**. Nie jest uniwersalnym backupem na inny ESP32: eFuse i klucze bezpieczeństwa nie znajdują się w obrazie flash.

**Odtwarzanie pełnego obrazu — tylko awaryjnie na tej samej płytce**, po sprawdzeniu identyfikatora chipu, rozmiaru obrazu, SHA256 oraz statusu flash encryption / secure boot:
```powershell
py -m esptool --chip esp32 --port COMx write-flash 0x0 esp32-before-m1-full-flash.bin
```
Nie używać `erase-flash` ani `--force` w typowej procedurze. Cały zapis flash zastępuje także bieżące konfiguracje, NVS, firmware i LittleFS ich zawartością **z momentu wykonania kopii**. To operacja wyłącznie odzyskiwania, nie standardowa metoda aktualizacji. Gdy zabezpieczenia flash są aktywne albo rozmiar danych się nie zgadza, przerwać i zweryfikować procedurę.

Źródło poleceń: oficjalne `esptool` Espressif, polecenia `flash-id`, `read-flash`, `write-flash`: https://docs.espressif.com/projects/esptool/en/latest/esp32/esptool/basic-commands.html

## Normalne aktualizacje M1, M2, ...: OTA, nie pełny flash

- Po potwierdzeniu backupów pobrać zatwierdzony, skompilowany dla `esp32dev` obraz **OTA aplikacji** z kontrolowanego commita/CI i wgrać przez istniejący panel `System → Firmware update` (obecny `POST /api/update` używa `Update.begin(UPDATE_SIZE_UNKNOWN,U_FLASH)`). Nie używać `factory.bin` jako OTA.
- OTA zapisuje partycję aplikacji i zwykle zachowuje pozostałe dane LittleFS/NVS, dlatego istniejąca konfiguracja nie powinna się wyzerować; jednak wymaga sprawdzenia na tym urządzeniu, nie stanowi zamiennika kopii flash.
- Nigdy nie modyfikować układu partycji podczas zwykłego OTA, nie używać `erase-flash` i nie wgrywać obrazu dla `esp32s3`/`esp32c3` na klasyczny `esp32dev`.
- Przed każdą wersją zachować: aktualny backup JSON, działający poprzedni obraz OTA, commit nowej wersji i hash pliku. Po aktualizacji sprawdzić web UI, MQTT, PID-y i identyfikatory encji.
- **Ryzyko wymagające późniejszego utwardzenia:** w obecnym `src/config.cpp` `LittleFS.begin(true)` może **sformatować filesystem po nieudanym montowaniu**; z tego powodu backup przed testem jest obowiązkowy. Rozważyć zmianę na tryb niewykonujący automatycznego formatowania i jawne odzyskiwanie.

## Kontrakt zachowania obecnego HA — MUST NOT CHANGE

Kod w `src/mqtt_ha.cpp` tworzy identyfikator urządzenia z ostatnich 3 bajtów adresu MAC Wi-Fi. `unique_id` encji to `deviceId() + "_" + objId`, a ścieżka Discovery to `haPrefix/component/deviceId/objId/config`.

Dla **tego samego fizycznego ESP32** i aktualizacji OTA:
- zachować bez zmiany `cfg.mqttBase`, `cfg.haPrefix`, `deviceId()` / MAC, dotychczasowe ID wszystkich sensorów PID i istniejące `unique_id`;
- nie tworzyć drugiego urządzenia Home Assistant przez zmianę prefiksu MQTT lub schematu ID; **nie usuwać** starej integracji ani encji przed testem;
- nowe encje BLE dołączać addytywnie, pod nowymi stabilnymi identyfikatorami; ich pojawienie się nie wymaga ponownej integracji MQTT po stronie HA;
- nie zakładać, że przywrócenie pełnego flash na **innej** fizycznej płytce zachowa `deviceId()`, bo MAC fizyczny będzie inny;
- przed release wykonywać regresję: urządzenie MQTT pozostało jedno, istniejące encje mają te same `unique_id`, entity IDs / automatyzacje HA działają; nowe encje się pojawiają; usunięte encje są czyszczone jawnie;
- zmianę MQTT base, HA prefix lub struktury `unique_id` traktować jak **migrację wymagającą osobnego planu**, nie zwykły update.

## Bramka przed M1 na jedynym hardware

- [ ] Zapisano JSON `with_secrets=true` i sprawdzono obecność wszystkich profili
- [ ] Zgrano pełny obraz flash przez USB i zapisano SHA256
- [ ] Drugi zrzut lub równoważna kontrola potwierdziły spójność obrazu
- [ ] Zanotowano COM, chip/flash size, IP, MAC, wersję firmware oraz MQTT `deviceId`, `mqtt_base` i `ha_prefix` **bez ujawniania haseł**
- [ ] Potwierdzono, że istniejące urządzenie i encje HA działają
- [ ] Wybrano właściwy build OTA `esp32dev` i bezpieczny sposób odtworzenia przy awarii
- [ ] Użytkownik osobno zatwierdził wgrywanie na fizyczne ESP32

**Stan:** Nie wykonano żadnego z punktów na fizycznym ESP32; ten dokument jest procedurą, a nie potwierdzeniem backupu.
