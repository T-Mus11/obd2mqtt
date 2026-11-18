# BLE Presence + OBD2MQTT — backlog produktu i Definition of Done

**Status:** Draft v0.1 — do zatwierdzenia; **data:** 2026-11-18  
**Repo:** `T-Mus11/obd2mqtt`; **baza:** `main` @ `f830e845c3bb8db1926860fb2d2448cf3e316a82` (v0.3.24).  
**Cel:** wersjonowany backlog wymagań. Projekt klas, API, podział modułów i implementacja — **po** zatwierdzeniu stories.

## Założenia i granice

- Monitorujemy **obserwowalność BLE**, nie fizyczną obecność auta. Vgate może zgasnąć w zaparkowanym samochodzie; `NOT_IN_RANGE` nie dowodzi odjazdu.
- Stany: `UNKNOWN`, `IN_RANGE`, `NOT_IN_RANGE`. Nie emitujemy fałszywych `arrival` / `departure`.
- RSSI i `APPROACHING / RECEDING / STABLE / INSUFFICIENT_DATA` to heurystyki, nie dowody ruchu.
- Wspólny skaner dla wielu BLE; pasywny odbiór reklam bez otwierania sesji OBD. Urządzenia z rotującym MAC wymagają stabilnego identyfikatora reklamy; nie wszystkie da się śledzić.
- Przejście `NOT_IN_RANGE → IN_RANGE` może **opcjonalnie** wyzwalać ograniczony odczyt OBD. `UNKNOWN → IN_RANGE` nie uruchamia automatycznej próby. Obserwowane wybudzenie Vgate po samym otwarciu drzwi (bez kluczyka/zapłonu) oznacza, że takie przejście **nie potwierdza aktywnego ECU ani przyjazdu**; próby OBD muszą być limitowane.
- Lokalnie, deterministycznie, fail-safe; brak dodatkowych usług chmurowych. Bez zmian w produkcji, wgrywania firmware i automatycznego merge.

## Epic A — Rejestr BLE

### US-01 [P0] — Wiele monitorowanych urządzeń
**Jako** administrator **chcę** dodawać, edytować, wyłączać i usuwać wiele urządzeń BLE **aby** monitorować samochody i tagi jednym ESP32.
**Akceptacja:** stabilny ID, nazwa, metoda identyfikacji i flaga enabled; walidacja duplikatów; trwałość po restarcie; usuwanie nie narusza innych konfiguracji; limit liczby urządzeń ustalony na podstawie pomiarów RAM/CPU, nie obietnicy.

### US-02 [P0] — Wyszukiwanie i wybór
**Jako** administrator **chcę** wyszukiwać reklamy BLE w panelu **aby** dodać urządzenie bez ręcznego wpisywania MAC.
**Akceptacja:** widoczne adres, nazwa (jeśli jest), RSSI i wspierane pola reklamy; brak nazwy nie blokuje wyboru; świeże i zapamiętane wykrycia rozróżnione; skan nie niszczy ustawień ani sesji OBD.

### US-03 [P1] — Metody identyfikacji
**Jako** administrator **chcę** dopasowywać stały MAC lub stabilny identyfikator reklamowy **aby** monitorować kompatybilne tagi.
**Akceptacja:** stały MAC działa; wspierane wybrane jednoznaczne pola reklamy; sama nazwa/RSSI nie identyfikuje urządzenia; rotacyjne MAC bez stabilnego identyfikatora jawnie niewspierane; brak fałszywego przypisania do innego urządzenia.

## Epic B — Stany i czas obserwacji

### US-04 [P0] — Jawny stan BLE
**Jako** użytkownik HA **chcę** otrzymywać `UNKNOWN / IN_RANGE / NOT_IN_RANGE` **aby** uniknąć błędnej interpretacji uśpienia adaptera.
**Akceptacja:** `IN_RANGE` po potwierdzonych reklamach; `NOT_IN_RANGE` po skonfigurowanym czasie **rzeczywiście wykonanego** skanowania bez wykrycia; `UNKNOWN` na starcie i przy niewystarczających danych; brak skanowania nie powoduje samoczynnego NOT_IN_RANGE; zmiany są rejestrowane.

### US-05 [P0] — Histereza i odporność na zaniki
**Jako** użytkownik **chcę** filtrowania przypadkowych reklam i przerw **aby** stany nie przełączały się chaotycznie.
**Akceptacja:** parametry potwierdzania wejścia/zaniku konfigurowalne; pojedynczy brak pakietu nie przełącza stanu; czas skanowania utracony przez OBD/Wi-Fi nie liczy się do potwierdzenia zniknięcia; wartości początkowe oznaczone jako do kalibracji.

### US-06 [P1] — Ostatnia obserwacja i zmiana
**Jako** użytkownik **chcę** znać `last_seen` i `state_since` **aby** rozumieć świeżość stanu.
**Akceptacja:** znaczniki czasu i jawna informacja o braku poprawnego zegara; ostatni historyczny pomiar po restarcie nie jest prezentowany jako nowy; odporność na zawinięcie `millis()`.

## Epic C — RSSI, trendy i diagnostyka

### US-07 [P0] — Próbki i wygładzenie RSSI
**Jako** użytkownik **chcę** widzieć zmiany RSSI **aby** analizować zasięg i możliwy ruch.
**Akceptacja:** próbki ze znacznikami czasu; surowy i wygładzony RSSI; krótki bufor w RAM; brak reklam = brak pomiaru (nie sztuczne -100 dBm); ograniczona liczba zapisów flash i publikacji MQTT; zdefiniowane zachowanie przy duplikatach reklam.

### US-08 [P1] — Trend sygnału
**Jako** użytkownik **chcę** trend `APPROACHING / RECEDING / STABLE / INSUFFICIENT_DATA` **aby** wnioskować ostrożnie o kierunku zmian sygnału.
**Akceptacja:** trend wyznaczany z wielu niezależnych okien i uwzględnia zmienność; jeden skok RSSI nie decyduje; brak próbek daje INSUFFICIENT_DATA; brak twierdzenia o fizycznym ruchu; progi podlegają kalibracji po testach terenowych.

### US-09 [P1] — Diagnostyka i eksport
**Jako** administrator **chcę** eksportować zdarzenia i pomiary **aby** analizować Vgate i tagi.
**Akceptacja:** log obejmuje wykrycia, zmiany stanów, RSSI/trend, przerwy i błędy skanowania; eksport do dalszej analizy (format do ustalenia w projekcie); limity pamięci i retencji; bez haseł, tokenów i nadmiernego zapisu flash.

## Epic D — Home Assistant / MQTT

### US-10 [P0] — MQTT Discovery
**Jako** użytkownik HA **chcę** automatycznych encji każdego BLE **aby** korzystać z dashboardów i automatyzacji.
**Akceptacja:** stabilny `unique_id` per urządzenie; binary_sensor dla `IN_RANGE` i osobna informacja o `UNKNOWN` (np. availability/diagnostic state), RSSI, trend i timestamps; brak duplikatów po restarcie; poprawne usuwanie discovery po usunięciu konfiguracji; niedostępność ESP32 ≠ NOT_IN_RANGE.

### US-11 [P0] — Zdarzenia zmian zasięgu
**Jako** autor automatyzacji HA **chcę** zdarzeń `NOT_IN_RANGE → IN_RANGE` i odwrotnie **aby** reagować lokalnie.
**Akceptacja:** identyfikator i czas zdarzenia; nie nazywamy ich arrival/departure; bez generowania pozornego przejścia przy restarcie (`UNKNOWN → IN_RANGE`); brak duplikacji zdarzeń przy ponownym wysłaniu danych MQTT.

## Epic E — OBD jako akcja opcjonalna

### US-12 [P1] — Odpytywanie po ponownym wykryciu
**Jako** właściciel auta **chcę** opcjonalnie uruchomić OBD po `NOT_IN_RANGE → IN_RANGE` **aby** zebrać aktualne dane ECU bez ciągłego pollingu.
**Akceptacja:** opcja domyślnie OFF; powiązanie wybranego BLE z **jednym** właściwym adapterem/profilami wspieranymi przez urządzenie; korzysta z istniejącego mechanizmu poll (nie równoległego ELM); `UNKNOWN → IN_RANGE` nie wyzwala; ręczne Poll now nadal działa; nie mylimy wykrycia adaptera z przyjazdem; po samym otwarciu drzwi Vgate może rozgłaszać BLE, choć ECU nadal nie odpowiada — brak odpowiedzi nie jest traktowany jako sukces, a kolejne próby podlegają US-13/US-14.

### US-13 [P1] — Kontrolowane ponawianie
**Jako** właściciel auta **chcę** ograniczonych prób po niepowodzeniu **aby** nie tracić krótkiego okna aktywnego ECU.
**Akceptacja:** limit prób, odstępów i całego okna; następna próba dopiero po zakończeniu poprzedniej; sukces kończy serię; kolejna reklama nie uruchamia równoległej serii; jawny wynik i przyczyna błędu w HA; zachowanie przy utracie BLE/Wi-Fi/MQTT określone.

### US-14 [P0, bramka bezpieczeństwa dla US-12] — Ochrona akumulatora
**Jako** właściciel auta **chcę** minimalnego wpływu na akumulator **aby** Vgate mógł pozostać w gnieździe na postoju.
**Akceptacja:** samo monitorowanie nie łączy się z ELM; po serii rozłącza BLE; limity i cooldown zapobiegają ciągłym pobudkom; restart nie uruchamia automatycznego poll; fabryczne ograniczenia napięcia nie są bez uzasadnienia wyłączane; odczyt `ATRV` traktowany ostrożnie (wcześniejsze zaniżenie ~0,9 V wymaga weryfikacji); wykonany test postoju.

## Epic F — Panel użytkownika

### US-15 [P1] — Konfiguracja i stan w WWW
**Jako** administrator **chcę** konfigurować detektor w panelu **aby** nie edytować plików ręcznie.
**Akceptacja:** lista urządzeń, enable/disable, status, RSSI/trend, ostatnia obserwacja; walidacja; ustawienia trwałe i uwzględnione w eksporcie/importowaniu konfiguracji; istniejący profil OBD nie jest zmieniany przez sam BLE Presence.

## Epic G — Niezawodność i testy

### US-16 [P0] — Koegzystencja skanera i połączenia OBD
**Jako** użytkownik OBD2MQTT **chcę** braku regresji BLE/Wi-Fi/ELM **aby** zachować niezawodność.
**Akceptacja:** jeden skaner i deterministyczna koordynacja z sesjami ELM; brak fałszywego NOT_IN_RANGE przy przerwie na OBD; watchdog działa, brak nieograniczonych blokad/kolejek; testy 1, 5, 10 urządzeń z pomiarami pamięci, CPU, Wi-Fi i utraconych reklam; odzyskiwanie po awarii.

### US-17 [P0] — Zgodność wstecz z v0.3.24
**Jako** obecny użytkownik **chcę** zachować profile, UI i MQTT **aby** aktualizacja była bezpieczna.
**Akceptacja:** działają JSON profili, terminal AT, ręczne Poll now, discovery i logi; istniejące dane konfiguracyjne czytane bez ręcznych migracji; nowa funkcja domyślnie OFF; zachowane hasła i konfiguracja (nie publikowane w kodzie); sprawdzony plan rollback.

### US-18 [P0] — Testy terenowe i reprodukowalność
**Jako** administrator **chcę** udokumentowanych scenariuszy testowych **aby** odróżnić uśpienie Vgate od wyjazdu.
**Akceptacja:** badanie zaparkowanego auta z wyłączonym zapłonem, zaśnięcia/wybudzenia Vgate **w tym po otwarciu drzwi bez kluczyka**, faktycznego wyjazdu/powrotu, innych tagów, reboot ESP32, utraty Wi-Fi/MQTT i dłuższej sesji OBD; zapis obserwacji RSSI i czasu, bez wyciągania pewnych wniosków o fizycznym ruchu; wyniki i nierozstrzygnięte hipotezy udokumentowane.

## Definition of Done — wspólne

Story jest DONE, jeśli **wszystkie mające zastosowanie** punkty są spełnione:

- [ ] Wymagania i kryteria akceptacji przeglądnięte, spełnione i udokumentowane.
- [ ] Stany błędne, nieznane i graniczne określone; brak mylących deklaracji fizycznej obecności.
- [ ] Implementacja jest na osobnej gałęzi, podlega przeglądowi PR; bez sekretów.
- [ ] Kod kompiluje się dla odpowiednich targetów i przeszedł testy regresji istniejącego firmware.
- [ ] Zweryfikowano zachowanie przy restartach, błędach komunikacji i `millis()` wraparound.
- [ ] Zmierzono RAM/CPU, stabilność Wi-Fi/BLE, watchdog i obciążenie flash/MQTT.
- [ ] MQTT Discovery ma stabilne identyfikatory, poprawną dostępność, brak sierot/duplikatów.
- [ ] OBD jest bezpiecznie ograniczone: brak równoległych połączeń, limit prób i cooldown; test wpływu na akumulator.
- [ ] Zaktualizowano dokumentację użytkownika, diagnozę, ograniczenia oraz procedurę aktualizacji i rollback.
- [ ] PR podaje pliki, commit, rzeczywiste testy, wyniki, ryzyka i braki.
- [ ] Brak merge ani wdrożenia produkcyjnego bez odrębnej zgody właściciela.

Dla **samych stories dokumentacyjnych** DoD: spójne i testowalne kryteria, przegląd oraz zapis w repozytorium; wymagania testowania firmware nie obowiązują, dopóki nie ma implementacji.

## Kolejność i zależności

| Milestone | Stories | Rezultat |
|---|---|---|
| M0 Backlog | Ten dokument | Uzgodnione wymagania i DoD |
| M1 Presence MVP | US-01,02,04,05,07,10,11,16,17 | Multi-BLE, stan, RSSI, HA |
| M2 Diagnostyka | US-06,08,09,18 | Trend i pomiary terenowe |
| M3 OBD Trigger | US-12,13,14 | Bezpieczne, ograniczone odczyty |
| M4 Rozszerzenia | US-03,15 | Identyfikacja tagów i panel |

US-14 jest **bramką** dla automatycznych odczytów; US-16/17 są obowiązkowe przy każdym etapie implementacji.

## Obserwacje terenowe — Nissan Qashqai / Vgate iCar Pro BLE (2026-11-18)

**Konfiguracja testu:** Nissan Qashqai J10, adapter Vgate iCar Pro BLE (nazwa `IOS-Vlink`, MAC `D2:E0:2F:8D:52:48`), ESP32 OBD2MQTT v0.3.24; ręczne „Scan for dongles”. `Polling: OFF`.

| Okoliczność | Wynik zaobserwowany |
|---|---|
| Zapłon włączony | Vgate widoczny przez BLE; wcześniej potwierdzono odpowiedzi ECU na PID-y |
| Około 1 min po wyjęciu kluczyka | Adapter nadal rozgłasza BLE, ECU zwraca `NO DATA` |
| Około 60 min po wyjęciu kluczyka | Vgate nieobecny w wynikach jednego ręcznego skanu; inne urządzenia były wykrywane |
| Otwarcie drzwi samochodu, bez wkładania kluczyka i bez włączania zapłonu | Adapter ponownie wykrywalny przez BLE — relacja z testu użytkownika |

**Interpretacja i ograniczenia:** wynik jest zgodny z hipotezą automatycznego uśpienia i wybudzenia adaptera przez aktywność elektryczną pojazdu, ale nie mierzy dokładnego czasu uśpienia, poboru prądu ani dostępności ECU **po otwarciu drzwi**. Jednorazowy brak w skanie nie dowodzi trwałego stanu `NOT_IN_RANGE`; musi go potwierdzić algorytm czasu skutecznego skanowania. Wznowienie reklam po otwarciu drzwi nie jest dowodem przyjazdu. Weryfikacja, czy PID-y odpowiadają już w tym momencie, pozostaje otwarta.

**Konsekwencje dla stories:** US-04/05 — klasyfikacja tylko na podstawie rzeczywistego skanowania; US-11 — bez etykiet arrival/departure; US-12/13/14 — odczyt wyłącznie opcjonalny, ograniczony, brak założenia aktywnego ECU; US-18 — obowiązkowy scenariusz testu otwarcia drzwi bez zapłonu.

## Decyzje otwarte (nie rozstrzygać przed pomiarami)

- Interwał/okna skanowania i budżet radiowy przy Wi-Fi + NimBLE + ELM.
- Liczba urządzeń i retencja próbek przy dostępnej pamięci ESP32-WROOM-32D.
- Okna potwierdzania, agregacja RSSI, filtr trendu i klasyfikacja danych niewystarczających.
- Mechanizmy identyfikacji tagów o zmiennych adresach.
- Format eksportu logów i szczegóły discovery/availability HA.
- Tryb harmonogramowania OBD przy wielu adapterach (pierwszy etap: wyłącznie wspierana istniejąca sesja ELM).
- Liczba ponowień/cooldown i walidacja napięcia Vgate względem multimetru.
- Analiza po testach uśpienia Vgate: BLE nieobecny może oznaczać sleep, nie oddalenie.

## Zweryfikowane punkty wejścia aktualnego kodu

- `src/elm_ble.cpp`: `ElmBle::scan(uint32_t ms)`, callbacki NimBLE, adres i RSSI.
- `src/poller.cpp`: `Poller::requestPollNow()`, kolejka zadań, okresowe polle i backoff.
- `src/web.cpp`: `POST /api/pollnow`.
- `src/mqtt_ha.cpp`: publikacja MQTT i Discovery.
- `README.md`: obecny zakres firmware i opis oszczędzania energii.

Żadne z powyższych nie oznacza, że nowa funkcja jest już zaimplementowana.
