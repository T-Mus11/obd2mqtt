# BLE Presence + OBD2MQTT — backlog produktu i Definition of Done

**Status:** Draft v0.2 — refinement architektury HA-centric; **aktualizacja:** 2026-11-20  
**Repo:** `T-Mus11/obd2mqtt`; **baza:** `main` @ `f830e845c3bb8db1926860fb2d2448cf3e316a82` (v0.3.24).  
**Cel:** wersjonowany backlog wymagań. Projekt klas, API, podział modułów i implementacja — **po** zatwierdzeniu stories.

## Podział odpowiedzialności — reguła nadrzędna

**ESP32 = lekki, lokalny adapter wejścia/wyjścia (BLE/OBD/MQTT); HA = orkiestrator, logika biznesowa i repozytorium historii.** Bez połączenia z HA ESP nie uruchamia własnych automatyzacji czy harmonogramów pojazdów. Minimalne filtrowanie obecności, kolejka poleceń i ochrona sprzętu po stronie ESP są konieczne do rzetelnej telemetrii i bezpieczeństwa.

| Odpowiedzialność | ESP32/firmware | Home Assistant |
|---|---|---|
| Skanowanie BLE i identyfikacja wielu urządzeń | Tak — jeden koordynowany skaner | Konfiguruje rejestr / odbiera obserwacje |
| Stan BLE i jakość obserwacji | Minimalny `UNKNOWN/IN_RANGE/NOT_IN_RANGE`, histereza, `last_seen`, RSSI | Historia, dodatkowe reguły i interpretacja |
| RSSI i trend ruchu | Próbki/krótkie agregaty; bez klasyfikacji ruchu | Trendy `APPROACHING/RECEDING/STABLE`, wykresy |
| MQTT Discovery, stany, odpowiedzi | Tak — telemetria i status wykonania komend | Konsumuje encje i zdarzenia |
| Decyzja o odczycie PID | Tylko na komendę lub ręczne żądanie | Wyzwalanie, okno 10 min, ponowienia co 30 s |
| `poll_data(adapter_id)` | Wykonanie jednej ograniczonej sesji dla wybranego adaptera/profilu | Wywołanie, korelacja, ponowienia, historia |
| Diagnostyka DTC | Odczyt danych surowych na żądanie | Wykrywanie nowych DTC, interpretacja, rekomendacje |
| Bezpieczeństwo | Limity twarde, timeout, jedna sesja BLE/ELM, cooldown i rozłączanie | Dodatkowe reguły i harmonogramy, ale nie zastępują zabezpieczeń ESP |

## Założenia i granice

- Monitorujemy **wykrywalność reklam BLE**, nie fizyczną obecność auta. `NOT_IN_RANGE` nie dowodzi wyjazdu (Vgate może zasnąć w stojącym samochodzie).
- Stany wykrywalności: `UNKNOWN`, `IN_RANGE`, `NOT_IN_RANGE`; bez zdarzeń nazwanych `arrival` / `departure`. Wznowienie reklam po otwarciu drzwi nie potwierdza zapłonu ani gotowości ECU.
- Jeden ESP może monitorować wiele adapterów OBD i tagów BLE. Każdy adapter OBD ma stabilne `adapter_id`, własny BLE identyfikator i przypisany profil/konfigurację inicjalizacji; wyniki nie mogą się mieszać.
- Minimalna histereza po stronie ESP chroni jakość telemetrii. Historia długoterminowa i analiza trendu RSSI należą do HA, nie do firmware.
- ESP **nie uruchamia automatycznie odczytu** po `NOT_IN_RANGE → IN_RANGE`; jedynie publikuje stan/zdarzenie. HA sam decyduje, czy i kiedy wysłać `poll_data(adapter_id)`.
- Okno prób 10 min / co 30 s jest regułą automatyzacji **HA**. Jedno wywołanie oznacza jedną próbę na ESP, nie uruchamia autonomicznej serii. ESP wymusza niezależne od HA ograniczenia bezpieczeństwa.
- Adapter może być w zasięgu i nadal zwracać `NO DATA`; to prawidłowy, odrębny rezultat operacji, nie awaria detekcji BLE.
- Brak komunikacji MQTT/HA nie powoduje samoczynnego odpytywania ECU. Komendy nie mogą być retained; zaległe, zdublowane lub przedawnione polecenia nie mogą uruchamiać sesji po restarcie.
- Zgodność wstecz: istniejące profile, ręczne `Poll now`, terminal i normalny polling v0.3.24 pozostają dostępne; nowy tryb sterowania z HA jest opt-in. Przed jego aktywacją istniejący autonomiczny polling należy jawnie wyłączyć dla objętych adapterów.
- Lokalne działanie, brak zależności od chmury. Bez automatycznego merge, firmware upload czy produkcyjnego wdrożenia.

## Epic A — Rejestr BLE

### US-01 [P0] — Wiele monitorowanych urządzeń
**Jako** administrator **chcę** dodawać, edytować, wyłączać i usuwać wiele urządzeń BLE **aby** monitorować samochody i tagi jednym ESP32.
**Akceptacja:** stabilny ID, nazwa, metoda identyfikacji i flaga enabled; adapter OBD może dodatkowo wskazać indywidualny profil/parametry ELM, a tag nie wymaga profilu; walidacja duplikatów; trwałość po restarcie; usuwanie nie narusza innych konfiguracji; limit liczby urządzeń ustalony na podstawie pomiarów RAM/CPU.

### US-02 [P0] — Wyszukiwanie i wybór
**Jako** administrator **chcę** wyszukiwać reklamy BLE w panelu **aby** dodać urządzenie bez ręcznego wpisywania MAC.
**Akceptacja:** widoczne adres, nazwa (jeśli jest), RSSI i wspierane pola reklamy; brak nazwy nie blokuje wyboru; świeże i zapamiętane wykrycia rozróżnione; skan nie niszczy ustawień ani sesji OBD.

### US-03 [P1] — Metody identyfikacji
**Jako** administrator **chcę** dopasowywać stały MAC lub stabilny identyfikator reklamowy **aby** monitorować kompatybilne tagi.
**Akceptacja:** stały MAC działa; wspierane wybrane jednoznaczne pola reklamy; sama nazwa/RSSI nie identyfikuje urządzenia; rotacyjne MAC bez stabilnego identyfikatora jawnie niewspierane; brak fałszywego przypisania do innego urządzenia.

## Epic B — Stany i czas obserwacji

### US-04 [P0] — Jawny stan BLE
**Jako** użytkownik HA **chcę** otrzymywać `UNKNOWN / IN_RANGE / NOT_IN_RANGE` **aby** uniknąć błędnej interpretacji uśpienia adaptera.
**Akceptacja:** minimalna maszyna stanów na ESP: `IN_RANGE` po potwierdzonych reklamach, `NOT_IN_RANGE` po skonfigurowanym czasie **rzeczywiście wykonanego** skanowania bez wykrycia, `UNKNOWN` na starcie i przy niewystarczających danych; pauzy na OBD nie są uznawane za brak BLE; każda zmiana ma identyfikator urządzenia, czas i publikację MQTT; szczegółową historię prowadzi HA.

### US-05 [P0] — Histereza i odporność na zaniki
**Jako** użytkownik **chcę** filtrowania przypadkowych reklam i przerw **aby** stany nie przełączały się chaotycznie.
**Akceptacja:** parametry potwierdzania wejścia/zaniku konfigurowalne; pojedynczy brak pakietu nie przełącza stanu; czas skanowania utracony przez OBD/Wi-Fi nie liczy się do potwierdzenia zniknięcia; wartości początkowe oznaczone jako do kalibracji.

### US-06 [P1] — Ostatnia obserwacja i zmiana
**Jako** użytkownik **chcę** znać `last_seen` i `state_since` **aby** rozumieć świeżość stanu.
**Akceptacja:** znaczniki czasu i jawna informacja o braku poprawnego zegara; ostatni historyczny pomiar po restarcie nie jest prezentowany jako nowy; odporność na zawinięcie `millis()`.

## Epic C — RSSI, trendy i diagnostyka

### US-07 [P0] — Próbki i wygładzenie RSSI
**Jako** użytkownik **chcę** widzieć zmiany RSSI **aby** analizować zasięg i możliwy ruch.
**Akceptacja:** ESP publikuje bieżące RSSI i krótkie, ograniczone agregaty wraz ze znacznikiem czasu oraz jakością obserwacji; HA przechowuje historię i oblicza wygładzone serie; na ESP tylko mały bufor RAM; brak reklam = brak pomiaru (nie sztuczne -100 dBm); ograniczona liczba zapisów flash i publikacji MQTT; zdefiniowane zachowanie przy duplikatach reklam.

### US-08 [P1] — Trend sygnału
**Jako** użytkownik HA **chcę** wyznaczać w HA trend `APPROACHING / RECEDING / STABLE / INSUFFICIENT_DATA` z telemetrii RSSI **aby** ostrożnie analizować zmianę zasięgu.
**Akceptacja:** trend liczony w HA z wielu niezależnych okien; pojedynczy skok RSSI nie decyduje; brak próbek daje INSUFFICIENT_DATA; ESP nie implementuje klasyfikatora ruchu; progi kalibrowane po testach terenowych i nie oznaczają potwierdzonego fizycznego ruchu.

### US-09 [P1] — Diagnostyka i eksport
**Jako** administrator **chcę** eksportować zdarzenia i pomiary **aby** analizować Vgate i tagi.
**Akceptacja:** ESP publikuje zdarzenia, RSSI, przyczynę błędu i metryki skanowania; log firmware to mały bufor diagnostyczny; HA archiwizuje, analizuje i eksportuje historię/trendy; brak sekretów i nadmiernego zapisu flash ESP.

## Epic D — Home Assistant / MQTT

### US-10 [P0] — MQTT Discovery
**Jako** użytkownik HA **chcę** automatycznych encji każdego BLE **aby** korzystać z dashboardów i automatyzacji.
**Akceptacja:** stabilny `unique_id` per urządzenie, binary_sensor dla `IN_RANGE` i osobny status `UNKNOWN` (np. availability/diagnostic state), RSSI, timestamps i jakość obserwacji; trend może tworzyć HA na podstawie RSSI; brak duplikatów po restarcie, czyszczenie Discovery po usunięciu urządzenia, niedostępność ESP32 ≠ NOT_IN_RANGE; odpowiedzi OBD są przypisane do właściwego adaptera.

### US-11 [P0] — Zdarzenia zmian zasięgu
**Jako** autor automatyzacji HA **chcę** zdarzeń `NOT_IN_RANGE → IN_RANGE` i odwrotnie **aby** reagować lokalnie.
**Akceptacja:** identyfikator i czas zdarzenia; nie nazywamy ich arrival/departure; bez generowania pozornego przejścia przy restarcie (`UNKNOWN → IN_RANGE`); brak duplikacji zdarzeń przy ponownym wysłaniu danych MQTT.

## Epic E — OBD jako akcja opcjonalna

### US-12 [P1] — Odpytywanie po ponownym wykryciu
**Jako** autor automatyzacji HA **chcę** móc wywołać `poll_data(adapter_id)` po otrzymaniu stanu BLE **aby** to HA, a nie ESP, decydował o odczycie.
**Akceptacja:** ESP publikuje przejścia zasięgu, ale nie inicjuje na ich podstawie OBD; HA może wywołać pojedynczą sesję dowolnego skonfigurowanego adaptera poprzez US-20/21; `UNKNOWN → IN_RANGE` nie jest automatycznym triggerem po restarcie; ręczne Poll now pozostaje możliwe; `NO DATA` po otwarciu drzwi to wynik próby, nie potwierdzenie zapłonu; dane i status wracają do HA z `adapter_id` oraz `request_id`.

### US-13 [P1] — Kontrolowane ponawianie
**Jako** autor automatyzacji HA **chcę** harmonogramować ograniczone próby `poll_data(adapter_id)` **aby** wykryć moment, w którym ECU zaczyna odpowiadać.
**Akceptacja:** HA zarządza oknem maks. 10 min, próbą początkową oraz nominalnym odstępem 30 s (maks. 20 prób, bez nakładania i nie częściej niż co 30 s); kończy po poprawnej odpowiedzi na wspierany PID, nie po samym BLE, `ATRV` lub `NO DATA`; po sukcesie może zlecić pełny odczyt profilu; timeout, cooldown, przerwanie po utracie stanu i odmowy ESP są jawne. ESP nie zna dziesięciominutowego harmonogramu; wykonuje wyłącznie pojedynczą komendę i odsyła wynik. Wartości 30 s/10 min wymagają walidacji poboru prądu.

### US-14 [P0, bramka bezpieczeństwa dla US-12] — Ochrona akumulatora
**Jako** właściciel auta **chcę** minimalnego wpływu na akumulator **aby** Vgate mógł pozostać w gnieździe na postoju.
**Akceptacja:** pasywne skanowanie nie otwiera sesji ELM; ESP wykonuje tylko zaakceptowane komendy HA, z niezależnym twardym limitem czasu operacji, bezpiecznym minimalnym odstępem, ograniczoną kolejką, pojedynczą sesją ELM, przerwaniem/rozłączeniem i ochroną przed zaległymi, retained lub zduplikowanymi poleceniami; po utracie HA/MQTT i restarcie brak samoczynnego odpytywania; dotychczasowy polling automatyczny pozostaje opcją legacy, nie działa równolegle dla urządzeń sterowanych z HA; zabezpieczenia napięcia nie są bez powodu wyłączane, a `ATRV` może zaniżać (~0,9 V zaobserwowane wcześniej); test postoju z drzwiami otwartymi, bez zapłonu przez 10 min przy sterowaniu HA. Wynik testu może wymusić niższe limity.

## Epic F — Panel użytkownika

### US-15 [P1] — Konfiguracja i stan w WWW
**Jako** administrator **chcę** konfigurować detektor w panelu **aby** nie edytować plików ręcznie.
**Akceptacja:** minimalna konfiguracja urządzeń BLE i ich profili/trybu wykonawczego, enable/disable, podgląd statusu i ostatniego RSSI; bez rozbudowanych wykresów, trendów ani harmonogramów w WWW (należą do HA); walidacja, trwałość i backup/restore; samo BLE Presence nie zmienia profili OBD.

## Epic G — Niezawodność i testy

### US-16 [P0] — Koegzystencja skanera i połączenia OBD
**Jako** użytkownik OBD2MQTT **chcę** braku regresji BLE/Wi-Fi/ELM **aby** zachować niezawodność.
**Akceptacja:** jeden skaner i deterministyczna koordynacja z sesjami ELM; brak fałszywego NOT_IN_RANGE przy przerwie na OBD; watchdog działa, brak nieograniczonych blokad/kolejek; testy 1, 5, 10 urządzeń z pomiarami pamięci, CPU, Wi-Fi i utraconych reklam; odzyskiwanie po awarii.

### US-17 [P0] — Zgodność wstecz z v0.3.24
**Jako** obecny użytkownik **chcę** zachować profile, UI i MQTT **aby** aktualizacja była bezpieczna.
**Akceptacja:** działają JSON profili, terminal AT, ręczne Poll now, discovery i logi; istniejące dane konfiguracyjne czytane bez ręcznych migracji; nowa funkcja domyślnie OFF; zachowane hasła i konfiguracja (nie publikowane w kodzie); sprawdzony plan rollback.

### US-18 [P0] — Testy terenowe i reprodukowalność
**Jako** administrator **chcę** udokumentowanych scenariuszy testowych **aby** odróżnić uśpienie Vgate od wyjazdu.
**Akceptacja:** zaparkowane auto bez zapłonu, zaśnięcie/wybudzenie po otwarciu drzwi, rzeczywisty wyjazd/powrót, inne tagi, reboot ESP32, utrata Wi-Fi/MQTT i długa sesja OBD; **HA** wywołuje `poll_data` po 1/5/9 min od otwarcia drzwi i przy braku zapłonu przez pełne 10 min, kontroluje odstęp 30 s, limity i zakończenie; ESP zwraca wynik każdej pojedynczej komendy; testy dwóch różnych adapterów/profili oraz pomiar poboru energii/czasu uśpienia; surowe obserwacje RSSI, bez pewnych wniosków o ruchu.

## Epic H — Diagnostyka DTC na żądanie (etap późniejszy, poza MVP)

### US-19 [P2] — Rozpoznawanie nowych błędów pojazdu i rekomendowanie działań
**Jako** system Home Assistant zarządzający wieloma pojazdami **chcę** wykrywać nowe kody usterek (DTC) przypisane do konkretnego adaptera/pojazdu i móc na żądanie wywołać ich szczegółowy odczyt, interpretację oraz ocenę pilności **aby** przekazać użytkownikowi zrozumiałą informację o problemie i zalecane kolejne działania.

**Kryteria akceptacji:**
1. Identyfikacja **pojazdu/adaptera** jest jednoznaczna (`adapter_id`), a historia usterek oddzielna dla każdego samochodu; nie mieszamy wyników między profilami.
2. HA porównuje **zestaw rzeczywiście odczytanych kodów DTC** z poprzednim poprawnym zestawem dla tego samego pojazdu. Odróżnia `new`, `still_present`, `no_longer_reported` oraz `unknown/not_read`; nie interpretuje braku odpowiedzi jako braku usterek. Odczyt samego MIL/licznika DTC nie jest wystarczający do ustalenia ich rodzaju.
3. Dostępne jest **jawne żądanie diagnostyczne dla wskazanego `adapter_id`**, odrębne od odczytu zwykłych PID-ów; kontrakt komend/odpowiedzi (MQTT/HA), `request_id`, timeout, status i korelacja odpowiedzi powstaną w osobnym refinement API wieloadapterowego. Żądanie może być wywołane z automatyzacji HA lub na polecenie użytkownika.
4. Odczyt obejmuje, **jeśli ECU i adapter obsługują**: Mode 03 (stored/confirmed), Mode 07 (pending), Mode 0A (permanent) oraz wybrane dostępne dane towarzyszące Mode 02 (freeze frame). Zapisywane są także surowa odpowiedź, tryb, czas, status odpowiedzi i źródło ECU, jeśli dostępne. Brak danych freeze frame lub danego trybu nie powoduje tworzenia domyślonych wartości.
5. System odróżnia **odczyt kodu DTC** od **jego interpretacji**. Standardowe opisy kodów mogą być używane z lokalnej, wersjonowanej bazy; interpretacje producenta wymagają odpowiedniego źródła dla marki/modelu/ECU. Kod specyficzny dla producenta bez wiarygodnej definicji otrzymuje status `interpretation_unknown`.
6. HA prezentuje: kod, typ (stored/pending/permanent), opis wraz ze źródłem, czas odczytu, dostępne parametry freeze frame, ocenę pilności i rekomendowane czynności (np. sprawdzenie dokumentacji, ponowna diagnostyka, wizyta w serwisie). Oddziela **fakty z ECU**, **interpretacje** i **rekomendacje**; poziom pewności oraz ograniczenia źródeł są jawne. Nie przedstawia diagnozy mechanicznej jako pewnej na podstawie samego kodu.
7. Powiadomienia o **nowych** kodach nie powtarzają się przy identycznych kolejnych odczytach; wymagana jest obsługa powrotu kodu po jego rzeczywistym zniknięciu. Brak odczytu i `NO DATA` są raportowane diagnostycznie, bez fałszywych alarmów o usunięciu usterki.
8. **Tryb tylko do odczytu:** automatyzacja nie używa Mode 04 (kasowanie DTC), nie wysyła komend aktuacyjnych i nie kasuje żadnych danych diagnostycznych. Ewentualne kasowanie błędów wymaga osobnej user story i wyraźnego potwierdzenia użytkownika.
9. Odczyt podlega globalnym limitom i kolejce sesji OBD (bez równoległego użycia BLE, z timeout, cooldown i ochroną akumulatora). Jeżeli ECU jest nieaktywne, zwracany jest jawny status, bez nieograniczonego ponawiania.
10. Testy obejmują: brak DTC, jeden i wiele kodów, stored/pending/permanent, nieobsługiwany tryb/freeze frame, brak ECU, zanik BLE i MQTT, dwa pojazdy, powracający kod, nieznany kod producenta i brak źródła interpretacji. Dane testowe są syntetyczne lub odczytowe; bez wywoływania rzeczywistej usterki i bez kasowania kodów.

**Zależności:** rejestr wielu adapterów (US-01), osobne encje/stan HA (US-10), twarde limity wykonawcze (US-14, US-16), API wywołań i wyniku (US-20/21), wieloprofilowość; funkcja po stabilizacji wieloadapterowego MVP. Interpretacja i rekomendacje pozostają wyłącznie w HA, nigdy w ESP.

**Poza zakresem:** naprawa pojazdu przez automatyzację, automatyczne kasowanie DTC, pełna diagnostyka producenta, gwarancja dostępności opisów dla każdego kodu oraz obowiązkowe użycie chmurowych modeli do interpretacji.

## Epic I — Wieloadapterowe API sterowane z HA (wymagane w MVP)

### US-20 [P0] — Wybór adaptera i profilu przy każdym odczycie
**Jako** Home Assistant **chcę** wskazać docelowy `adapter_id` podczas odczytu **aby** obsługiwać dwa lub więcej samochodów jednym ESP32.
**Akceptacja:** rejestr może powiązać każdy adapter OBD z MAC/identyfikatorem BLE, adresem BLE, profilem JSON i ustawieniami protokołu; jedno żądanie wybiera jeden adapter i jego profil; firmware serializuje sesje i odłącza poprzednią; nie miesza ustawień ELM ani wyników pojazdów; nieznany/wyłączony adapter zwraca jawny błąd; próby równoczesne są odrzucane jako `busy` lub kolejkowane w **małej kolejce ograniczonej**; test na dwóch adapterach i dwóch różnych profilach. Obecna implementacja v0.3.24 jest jednoadapterowa i wymaga refaktoryzacji.

### US-21 [P0] — Polecenie `poll_data(adapter_id)` i wynik do HA
**Jako** Home Assistant **chcę** wysłać przez MQTT żądanie pojedynczego odczytu i otrzymać skorelowany wynik **aby** samodzielnie planować kolejne działania.
**Akceptacja:** topic polecenia np. `obd2mqtt/command/poll_data` przyjmuje `request_id`, `adapter_id`, opcjonalny rodzaj `probe` (lekki sprawdzian gotowości ECU) lub `full` (profil); topic odpowiedzi zawiera `request_id`, `adapter_id`, jednoznaczny status (`success`, `no_ecu_response`, `adapter_unavailable`, `busy`, `timeout`, `invalid_request`, `rate_limited`), timestamp i dane z tego konkretnego auta; `probe` potwierdza odpowiedź prawidłowego PID, `full` zwraca dane ze zdefiniowanego profilu. Nie używamy retained dla komend; deduplikacja `request_id`, graniczny wiek żądania i bezpieczna obsługa restartu. Endpoint MQTT nie powinien umożliwiać dowolnych nieautoryzowanych komend ELM ani kasowania DTC; dostęp przez uprawnienia brokera, topic i konfigurację urządzenia. Dla HA udostępniamy wywoływalny skrypt/integrację mapującą komendę i odpowiedź, nie obiecujemy natywnej funkcji C++ w HA.

### US-22 [P0] — Orkiestracja odczytu w HA
**Jako** administrator HA **chcę** definiować automatyzację: `IN_RANGE` adaptera → serie żądań `poll_data(adapter_id)` **aby** mieć logikę prób i historii poza ESP.
**Akceptacja:** HA wyzwala żądania według własnych warunków, domyślnie po potwierdzonym `NOT_IN_RANGE → IN_RANGE` (nie po `UNKNOWN` przy starcie), najwyżej co 30 s przez 10 min; maks. 20 prób i brak nachodzenia; kończy na poprawnym `probe`, może wtedy wykonać jeden `full`; brak odpowiedzi, reconnect, uśpienie Vgate i restart HA mają jawny wynik; HA rejestruje osobną historię prób i danych dla obu aut; urządzenie ESP pozostaje wykonawcą pojedynczego polecenia bez autonomicznego 10-minutowego schedulera.

## Definition of Done — wspólne

Story jest DONE, jeśli **wszystkie mające zastosowanie** punkty są spełnione:

- [ ] Wymagania i kryteria akceptacji przeglądnięte, spełnione i udokumentowane.
- [ ] Stany błędne, nieznane i graniczne określone; brak mylących deklaracji fizycznej obecności.
- [ ] Implementacja jest na osobnej gałęzi, podlega przeglądowi PR; bez sekretów.
- [ ] Kod kompiluje się dla odpowiednich targetów i przeszedł testy regresji istniejącego firmware.
- [ ] Zweryfikowano zachowanie przy restartach, błędach komunikacji i `millis()` wraparound.
- [ ] Zmierzono RAM/CPU, stabilność Wi-Fi/BLE, watchdog i obciążenie flash/MQTT.
- [ ] MQTT Discovery ma stabilne identyfikatory, poprawną dostępność, brak sierot/duplikatów; wynik komendy zachowuje `adapter_id` i `request_id`, a telemetria pojazdów nie miesza się.
- [ ] OBD jest bezpiecznie ograniczone po stronie ESP: brak równoległych połączeń, twarde limity, cooldown, brak wykonania zaległych komend; HA steruje harmonogramem prób; test wpływu na akumulator.
- [ ] Zaktualizowano dokumentację użytkownika, kontrakt MQTT/HA, diagnozę, ograniczenia oraz procedurę aktualizacji i rollback.
- [ ] Testy obejmują dwa adaptery o różnych profilach, BLE tag, zerwanie MQTT, restart HA/ESP, zduplikowane/przedawnione komendy i brak ECU.
- [ ] PR podaje pliki, commit, rzeczywiste testy, wyniki, ryzyka i braki.
- [ ] Brak merge ani wdrożenia produkcyjnego bez odrębnej zgody właściciela.

Dla **samych stories dokumentacyjnych** DoD: spójne i testowalne kryteria, przegląd oraz zapis w repozytorium; wymagania testowania firmware nie obowiązują, dopóki nie ma implementacji.

## Kolejność i zależności

| Milestone | Stories | Rezultat |
|---|---|---|
| M0 Backlog | Ten dokument | Uzgodnione wymagania i DoD |
| M1 Presence MVP | US-01,02,04,05,07,10,11,16,17 | Lekki skaner i stany BLE/MQTT, podstawowa telemetria |
| M2 API wielu adapterów | US-20,21,14,16,17 | Jedna ograniczona sesja `poll_data(adapter_id)`, wynik do HA |
| M3 Automatyzacje HA | US-12,13,22,18 | Próby co 30 s przez 10 min i historia po stronie HA |
| M4 Rozszerzenia | US-03,06,08,09,15 | Identyfikacja tagów, trendy i eksport historii w HA, prosty panel konfiguracji |
| M5 Diagnostyka DTC | US-19 | ESP odczytuje DTC na żądanie, HA wykrywa nowości, interpretuje i rekomenduje, bez kasowania |

US-14 jest **bramką** dla automatycznych odczytów; US-16/17 są obowiązkowe przy każdym etapie implementacji. US-20/21/22 są obowiązkowe dla wieloadapterowego MVP sterowanego z HA. US-19 pozostaje poza MVP.

## Obserwacje terenowe — Nissan Qashqai / Vgate iCar Pro BLE (2026-11-18)

**Konfiguracja testu:** Nissan Qashqai J10, adapter Vgate iCar Pro BLE (nazwa `IOS-Vlink`, MAC `D2:E0:2F:8D:52:48`), ESP32 OBD2MQTT v0.3.24; ręczne „Scan for dongles”. `Polling: OFF`.

| Okoliczność | Wynik zaobserwowany |
|---|---|
| Zapłon włączony | Vgate widoczny przez BLE; wcześniej potwierdzono odpowiedzi ECU na PID-y |
| Około 1 min po wyjęciu kluczyka | Adapter nadal rozgłasza BLE, ECU zwraca `NO DATA` |
| Około 60 min po wyjęciu kluczyka | Vgate nieobecny w wynikach jednego ręcznego skanu; inne urządzenia były wykrywane |
| Otwarcie drzwi samochodu, bez wkładania kluczyka i bez włączania zapłonu | Adapter ponownie wykrywalny przez BLE, ale PID-y zwracają `NO DATA` — test użytkownika |

**Interpretacja i ograniczenia:** wynik jest zgodny z hipotezą automatycznego uśpienia i wybudzenia adaptera przez aktywność elektryczną pojazdu, ale nie mierzy dokładnego czasu uśpienia ani poboru prądu; **po otwarciu drzwi nie uzyskano odpowiedzi ECU na PID-y**. Jednorazowy brak w skanie nie dowodzi trwałego stanu `NOT_IN_RANGE`; musi go potwierdzić algorytm czasu skutecznego skanowania. Wznowienie reklam po otwarciu drzwi nie jest dowodem przyjazdu. Potwierdzenie gotowości ECU wymaga ponownego odczytu PID po przekręceniu kluczyka. Dla scenariusza wejścia do auta przed uruchomieniem zapłonu **HA**, nie ESP, ma zlecać pojedyncze próby PID przez maks. 10 minut, nominalnie co 30 sekund (US-13/22), z kontrolą wpływu na akumulator (US-14).

**Konsekwencje dla stories:** US-04/05 — klasyfikacja tylko z rzeczywistego skanowania; US-11 — bez etykiet arrival/departure; US-12/13/22 — HA wyzwala i powtarza komendy, ESP wykonuje pojedynczą próbę; US-14 — sprzętowe zabezpieczenia ESP niezależne od HA; US-18 — test otwarcia drzwi bez zapłonu.

## Decyzje otwarte (nie rozstrzygać przed pomiarami)

- Interwał/okna skanowania i budżet radiowy przy Wi-Fi + NimBLE + ELM.
- Liczba urządzeń i retencja próbek przy dostępnej pamięci ESP32-WROOM-32D.
- Okna potwierdzania, agregacja RSSI, filtr trendu i klasyfikacja danych niewystarczających.
- Mechanizmy identyfikacji tagów o zmiennych adresach.
- Format eksportu logów i szczegóły discovery/availability HA.
- Zasady małej kolejki/odrzucania zadań przy wielu adapterach, graniczny wiek wiadomości MQTT, deduplikacja i timeout. Istniejąca v0.3.24 używa jednej konfiguracji BLE/profilu, więc wymaga refaktoryzacji.
- Harmonogram HA 30 s / 10 min / 20 prób oraz twarde limity ESP: dokładne wartości po pomiarach wpływu na Vgate i akumulator; walidacja napięcia Vgate względem multimetru.
- Analiza po testach uśpienia Vgate: BLE nieobecny może oznaczać sleep, nie oddalenie.
- US-19 (późniejszy etap): zakres Mode 03/07/0A i Mode 02 dla konkretnych ECU, format surowych ramek, źródła opisów DTC, reguły ocen pilności, polityka lokalnego/ewentualnie zewnętrznego wyjaśniania usterek oraz kontrakt `read_dtc(adapter_id)`.

## Zweryfikowane punkty wejścia aktualnego kodu

- `src/elm_ble.cpp`: `ElmBle::scan(uint32_t ms)`, callbacki NimBLE, adres i RSSI.
- `src/poller.cpp`: `Poller::requestPollNow()`, kolejka zadań, okresowe polle i backoff.
- `src/web.cpp`: `POST /api/pollnow`.
- `src/mqtt_ha.cpp`: publikacja MQTT i Discovery.
- `README.md`: obecny zakres firmware i opis oszczędzania energii.

Żadne z powyższych nie oznacza, że nowa funkcja jest już zaimplementowana.
