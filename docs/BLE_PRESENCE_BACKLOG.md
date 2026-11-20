# BLE Presence + OBD2MQTT — backlog produktu i Definition of Done

**Status:** Draft v0.3 — refinement MVP (Vgate BLE jako jedyny automatyczny trigger, wykonanie i arbitraż ESP32); **aktualizacja:** 2026-11-20  
**Repo:** `T-Mus11/obd2mqtt`; **baza:** `main` @ `f830e845c3bb8db1926860fb2d2448cf3e316a82` (v0.3.24).  
**Cel:** wersjonowany backlog wymagań. Projekt klas, API, podział modułów i implementacja — **po** zatwierdzeniu stories.

## Refinement v0.3 — decyzje MVP z 20.11.2026

**Wiążące ustalenia:** celem jest Fiat Tipo SW 1.4 T-Jet LPG (Valencia), Qashqai (Polska) służy też do testów. Jedynym automatycznym wyzwalaczem diagnostyki jest BLE własnego adaptera Vgate OBD; tagi Xiaomi, kamery, BT Classic i Wi-Fi są sygnałami pomocniczymi i nie wyzwalają odczytu. HA wybiera auto i zakres danych oraz może wysłać jawne `poll_data`; ESP32 odpowiada za lekką lokalną maszynę stanów, retry, deduplikację i pojedynczy dostęp do ELM. Poniższe stories aktualizują wcześniejszy model HA-only v0.2; roboczy PR #2 BLE Presence nie jest wdrożeniem tego refinement.

### US-23 [P0] — Minimalny wynik diagnostyczny po wykryciu Vgate
**Jako** właściciel Fiata **chcę** po powrocie znać dystans i nowe kody błędów **aby** ocenić przebieg i stan auta bez niepotrzebnej telemetrii.
**Akceptacja:** komplet MVP to `0131` (km od ostatniego kasowania DTC), `0101` (MIL/liczba DTC emisji), Mode `03` (stored) i `07` (pending). HA oblicza różnicę `0131` tylko między poprawnymi pomiarami, wykrywa reset/spadek lub nasycenie licznika i nie zgaduje dystansu; porównuje rzeczywiste zestawy kodów, nie samą liczbę. Brak komunikacji, `NO DATA`, nieobsługiwany PID, odpowiedź zerowa i poprawne `0 DTC` to różne stany. Po pierwszym poprawnym PID z ECU natychmiast dokańczamy zestaw i ponawiamy tylko brakujące pola; dopiero pełny zestaw lub termin kończy sesję. Żadnego kasowania DTC (Mode 04). Opcjonalne później: Mode 0A/02, ABS/SRS/BCM, napięcie i TPMS.

### US-24 [P0] — Parametry czasowe konfigurowalne z HA i egzekwowane przez ESP
**Jako** administrator **chcę** zmieniać czasy bez aktualizacji firmware **aby** stroić odczyty po rzeczywistych testach. Proponowane wartości początkowe (niezweryfikowane na sprzęcie):

| Parametr | Start | Znaczenie |
|---|---:|---|
| `poll_window_s` | 600 s | Termin całej sesji od pierwszego zaakceptowanego triggera, także oczekiwanie w kolejce |
| `ble_scan_interval_s` | 3 s | Docelowy interwał kontroli obecności; nie długość okna skanu NimBLE; wymaga testu obciążenia |
| `retry_interval_s` | 5 s | Odstęp pomiędzy nieudanymi próbami komunikacji |
| `ble_connect_timeout_s` | 8 s | Timeout pojedynczego zestawienia BLE |
| `obd_query_timeout_s` | 2 s | Timeout pojedynczej odpowiedzi ECU (do weryfikacji przy wieloramkowych DTC) |
| `absence_confirm_s` | 180 s | Czas skutecznej nieobecności BLE przed ponownym uzbrojeniem |
| `cooldown_s` | 300 s | Blokada automatycznej sesji po zakończeniu, niezależna od warunku nieobecności |

**Akceptacja:** parametry konfigurowalne z HA, walidowane przez ESP z twardymi limitami i zapisane na restart; timeouty nie tworzą nieskończonych blokad. W czasie sesji kolejne reklamy Vgate ani zdarzenia pomocnicze nie resetują 600 s. Przy restarcie ESP stan początkowy `UNKNOWN` i brak skutecznych skanów nie mogą udawać nieobecności ani uruchamiać odczytu; nie wznawiać starych komend. Deduplikować aktywne sesje po `adapter_id`, a retransmisje MQTT po `request_id`; pole `reason` nie uczestniczy w deduplikacji. Pamiętać, że wybudzenie Vgate na postoju może wyglądać jak ponowne pojawienie się: nie gwarantujemy dokładnie jednego odczytu na fizyczny przyjazd.

**Testy akceptacyjne:** pięć niemal równoczesnych sygnałów (Vgate, dwa Xiaomi, kamera, Wi-Fi) daje najwyżej jedną automatyczną sesję; drugi `poll_data` z HA dla tego samego adaptera nie dubluje odczytu. Dwa auta: nowe żądanie drugiego otrzymuje możliwość próby bez 600 s czekania na pierwsze nieodpowiadające. Zmierzyć wolny RAM, rozmiar firmware, WDT, obciążenie skanera BLE + Wi-Fi, czas PID/DTC, wpływ na akumulator, zachowanie po zaniku MQTT i błędnych danych.

---

## Podział odpowiedzialności — reguła nadrzędna

**ESP32 = lekki lokalny wykonawca BLE/OBD/MQTT i właściciel kolejki, retry oraz timeoutów; HA = konfiguracja pojazdów i metryk, wywołania ręczne/na żądanie, historia i analiza.** Wyłącznie opt-in detekcja zarejestrowanego adaptera Vgate BLE może autonomicznie rozpocząć na ESP sesję odczytu. Pozostałe harmonogramy i triggery HA nie są potrzebne do automatycznego MVP.

| Odpowiedzialność | ESP32/firmware | Home Assistant |
|---|---|---|
| Skanowanie BLE i identyfikacja wielu urządzeń | Tak — jeden koordynowany skaner | Konfiguruje rejestr / odbiera obserwacje |
| Stan BLE i jakość obserwacji | Minimalny `UNKNOWN/IN_RANGE/NOT_IN_RANGE`, histereza, `last_seen`, RSSI | Historia, dodatkowe reguły i interpretacja |
| RSSI i trend ruchu | Próbki/krótkie agregaty; bez klasyfikacji ruchu | Trendy `APPROACHING/RECEDING/STABLE`, wykresy |
| MQTT Discovery, stany, odpowiedzi | Tak — telemetria i status wykonania komend | Konsumuje encje i zdarzenia |
| Decyzja o odczycie PID | Jedyny automatyczny trigger: Vgate BLE po ponownym uzbrojeniu; ponowienia lokalne do 600 s | Konfiguruje auto/profil/metryki, może jawnie zażądać `poll_data` |
| `poll_data(adapter_id)` | Przyjęcie, deduplikacja, kolejka i wykonanie kompletnego zestawu PID/DTC | Zlecenie na żądanie, korelacja wyniku, historia |
| Diagnostyka DTC | Odczyt danych surowych na żądanie | Wykrywanie nowych DTC, interpretacja, rekomendacje |
| Bezpieczeństwo | Limity twarde, timeout, jedna sesja BLE/ELM, cooldown i rozłączanie | Dodatkowe reguły i harmonogramy, ale nie zastępują zabezpieczeń ESP |

## Założenia i granice

- Monitorujemy **wykrywalność reklam BLE**, nie fizyczną obecność auta. `NOT_IN_RANGE` nie dowodzi wyjazdu (Vgate może zasnąć w stojącym samochodzie).
- Stany wykrywalności: `UNKNOWN`, `IN_RANGE`, `NOT_IN_RANGE`; bez zdarzeń nazwanych `arrival` / `departure`. Wznowienie reklam po otwarciu drzwi nie potwierdza zapłonu ani gotowości ECU.
- Jeden ESP może monitorować wiele adapterów OBD i tagów BLE. Każdy adapter OBD ma stabilne `adapter_id`, własny BLE identyfikator i przypisany profil/konfigurację inicjalizacji; wyniki nie mogą się mieszać.
- Minimalna histereza po stronie ESP chroni jakość telemetrii. Historia długoterminowa i analiza trendu RSSI należą do HA, nie do firmware.
- ESP w opt-in trybie **automatycznie uruchamia ograniczoną sesję tylko po wykryciu zarejestrowanego Vgate BLE**, po potwierdzonym okresie skutecznej nieobecności i cooldown; inne BLE/tags, kamera, BT Classic i Wi-Fi nie wyzwalają OBD. `UNKNOWN → IN_RANGE` po restarcie nie jest nowym przyjazdem.
- **ESP** egzekwuje okno prób do 600 s od triggera/żądania, ponawiając łączność i brakujące odczyty według lokalnej konfiguracji. **Nie** ma cyklu `poll_data` co 30 s w HA. Pierwsza poprawna odpowiedź ECU kończy agresywne próby połączenia, lecz nie kończy pobierania metryk i DTC.
- Adapter może być w zasięgu i nadal zwracać `NO DATA`; to prawidłowy, odrębny rezultat operacji, nie awaria detekcji BLE.
- Nowa automatyczna sesja Vgate może działać lokalnie według wcześniej zatwierdzonej i zapisanej konfiguracji mimo chwilowej niedostępności HA/MQTT; wyniki wymagają statusu dostarczenia. MQTT nie może uruchamiać przedawnionych, retained lub zduplikowanych poleceń; restart ESP nie może samoczynnie wznowić starej sesji.
- Zgodność wstecz: profile, ręczne `Poll now`, terminal i dotychczasowy polling v0.3.24 pozostają dostępne; nowy **automatyczny tryb Vgate/HA poll jest opt-in i wzajemnie wyklucza się z legacy autopoll dla tego samego adaptera**. Nie włączać na produkcji bez kontroli regresji.
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

### US-12 [P0] — Automatyczna sesja po wykryciu zarejestrowanego Vgate BLE
**Jako** właściciel pojazdu **chcę** automatycznie rozpocząć pojedynczą sesję diagnostyczną po ponownym wykryciu adaptera Vgate **aby** zdążyć z odczytem przed wyłączeniem zapłonu, bez zależności od innych triggerów.
**Akceptacja:** tryb opt-in, wyłącznie zarejestrowany adapter OBD i potwierdzone ponowne pojawienie BLE po skutecznej nieobecności wyzwalają lokalną sesję ESP; inne Xiaomi BLE, kamera, BT Classic czy Wi-Fi nie wyzwalają; `UNKNOWN → IN_RANGE` po restarcie nie uruchamia poll; BLE nie dowodzi fizycznego przyjazdu ani gotowości ECU; test uśpienia Vgate i wybudzenia po otwarciu drzwi; status i wynik publikowany do HA.

### US-13 [P0] — Lokalna sesja z ograniczonymi retry i zbieraniem brakujących pól
**Jako** administrator **chcę** aby ESP32 sam ponawiał krótkie próby połączenia i odczytu w jednym ograniczonym oknie **aby** nie stracić krótkiego czasu dostępności ECU i nie obciążać HA.
**Akceptacja:** ESP32 odlicza maks. 600 s od pierwszego zaakceptowanego triggera/żądania (również czas oczekiwania w kolejce); retry początkowo co 5 s; pierwsza poprawna odpowiedź PID z ECU kończy fazę prób ustanowienia komunikacji, po czym natychmiast następuje wymagany zestaw MVP `0131`, `0101`, `03`, `07`. Ponawia się tylko brakujące pola; komplet kończy wcześniej, timeout kończy częściowym/jawnym wynikiem. Samo BLE, odpowiedź `OK` ELM i `ATRV` nie są sukcesem ECU; `NO DATA` nie oznacza braku błędów. Kolejne reklamy ani triggery nie resetują zegara. Parametry walidowane i strojalne; test wpływu na akumulator.

### US-14 [P0, bramka bezpieczeństwa dla US-12] — Ochrona akumulatora
**Jako** właściciel auta **chcę** minimalnego wpływu na akumulator **aby** Vgate mógł pozostać w gnieździe na postoju.
**Akceptacja:** w trybie opt-in tylko wykrycie zarejestrowanego Vgate lub jawne polecenie HA uruchamia ograniczoną sesję ELM; brak ciągłego pollingu. Twarde limity, timeouty, cooldown, jednocześnie jedna sesja ELM, ograniczona kolejka, bezpieczne rozłączanie, deduplikacja i niewykonywanie starych poleceń; brak drugiej automatycznej sesji podczas postojowego wybudzenia przed zakończeniem cooldown/absence. Legacy autopoll nie działa równolegle. Test 10 min bez zapłonu, pomiar wpływu na akumulator; `ATRV` nie zastępuje pomiaru multimetrem.

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
**Akceptacja:** test zaparkowanego auta bez zapłonu, uśpienia/wybudzenia Vgate po otwarciu drzwi, rzeczywistego wyjazdu/powrotu, 5 niezależnych sygnałów obecności z jednym wyzwalaczem Vgate, resetu ESP, utraty Wi-Fi/MQTT, 10 min bez ECU i krótkiego okna dostępności ECU (ok. 60–90 s — założenie terenowe, nie gwarancja); dwie równoczesne komendy dla tego samego adaptera i dwa różne adaptery; pomiar obciążenia BLE/Wi-Fi i akumulatora, danych surowych i statusów; bez nazywania stanu BLE fizycznym przyjazdem.

## Epic H — Rozszerzona interpretacja DTC (poza MVP; surowe kody 03/07 są w MVP)

### US-19 [P2] — Interpretacja i dodatkowe tryby DTC po uzyskaniu kodów MVP
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

**Zależności:** rejestr wielu adapterów (US-01), raporty DTC MVP (US-23), osobne encje/stan HA (US-10), twarde limity wykonawcze (US-14/16), API (US-20/21). Rozszerzona interpretacja, Mode 0A, Mode 02 i moduły ABS/SRS/BCM są po stabilizacji MVP; podstawowe Mode 03/07 NIE są odłożone.

**Poza zakresem:** naprawa pojazdu przez automatyzację, automatyczne kasowanie DTC, pełna diagnostyka producenta, gwarancja dostępności opisów dla każdego kodu oraz obowiązkowe użycie chmurowych modeli do interpretacji.

## Epic I — Wieloadapterowe API sterowane z HA (wymagane w MVP)

### US-20 [P0] — Wybór adaptera i profilu przy każdym odczycie
**Jako** Home Assistant **chcę** wskazać docelowy `adapter_id` podczas odczytu **aby** obsługiwać dwa lub więcej samochodów jednym ESP32.
**Akceptacja:** rejestr łączy `adapter_id` z identyfikatorem BLE, profilem JSON i protokołem; nie miesza danych aut. ESP serializuje pojedynczą sesję BLE/ELM, ma ograniczony stan oczekiwania per adapter bez rozbudowanej dynamicznej kolejki; pierwsze próby nowych aut przed retry, po krótkiej nieudanej próbie radio przechodzi do kolejnego auta, ale trwający poprawny krótki zestaw PID kończy się bez przerywania. Przy przepełnieniu jawny `rejected`; test dwóch aut. Dla MVP sytuacja równoczesnego przyjazdu jest rzadka, ale nie może blokować drugiego auta pełne 600 s.

### US-21 [P0] — Polecenie `poll_data(adapter_id)` i wynik do HA
**Jako** Home Assistant **chcę** zlecić pojedynczą *kompletną ograniczoną sesję* dla `adapter_id` i wybranych metryk (również ręcznie) **aby** otrzymać wynik bez programowania retry w HA.
**Akceptacja:** topic np. `obd2mqtt/command/poll_data`: `request_id`, `adapter_id`, `metrics` (np. `mvp`), opcjonalny timeout i jawne `force` do ręcznego obejścia cooldown, nigdy limitów bezpieczeństwa. `reason` nie jest wymagany ani używany do deduplikacji. Potwierdzenie `accepted/already_running/queued/rejected` i wynik `success/partial/timeout/no_ecu_response/...` z `request_id`, `adapter_id`, timestampami i osobnymi statusami metryk. Deduplikacja retransmisji `request_id` i współdzielenie aktywnej sesji po `adapter_id`; różne zestawy metryk wymagają jawnej reguły (uzupełnienie albo odmowa), nie milczącego pominięcia. Komendy `retain=false`, limit wieku, bezpieczny restart; uprawnienia MQTT, brak nieautoryzowanych arbitralnych komend AT/Mode 04. HA może obsługiwać to skryptem/usługą, nie wymaga integracji chmurowej.

### US-22 [P0] — Konfiguracja wyzwalania i parametrów z HA
**Jako** administrator HA **chcę** ustawiać z HA zakres odczytu i strojalne czasy przy autonomicznym wykonaniu ESP **aby** nie wprowadzać każdego tuningu przez kompilację firmware.
**Akceptacja:** HA może skonfigurować profil/metryki i parametry US-24 oraz uruchomić jawne `poll_data`; automatyczny trigger to tylko Vgate BLE rozpoznany przez ESP. Nie ma HA-loop 30 s × 20; ESP jest właścicielem czasu, deduplikacji i kolejki; konfiguracja przetrwa restart, aktywne stare polecenia nie są wznawiane. HA archiwizuje wyniki, daty, korelacje, sukces/partial/no ECU i porównuje poprzednie poprawne odczyty.

## Definition of Done — wspólne

Story jest DONE, jeśli **wszystkie mające zastosowanie** punkty są spełnione:

- [ ] Wymagania i kryteria akceptacji przeglądnięte, spełnione i udokumentowane.
- [ ] Stany błędne, nieznane i graniczne określone; brak mylących deklaracji fizycznej obecności.
- [ ] Implementacja jest na osobnej gałęzi, podlega przeglądowi PR; bez sekretów.
- [ ] Kod kompiluje się dla odpowiednich targetów i przeszedł testy regresji istniejącego firmware.
- [ ] Zweryfikowano zachowanie przy restartach, błędach komunikacji i `millis()` wraparound.
- [ ] Zmierzono RAM/CPU, stabilność Wi-Fi/BLE, watchdog i obciążenie flash/MQTT.
- [ ] MQTT Discovery ma stabilne identyfikatory, poprawną dostępność, brak sierot/duplikatów; wynik komendy zachowuje `adapter_id` i `request_id`, a telemetria pojazdów nie miesza się.
- [ ] OBD jest bezpiecznie ograniczone na ESP: jedna sesja, twarde limity, cooldown, brak wykonania zaległych poleceń; lokalne retry i arbitraż; HA konfiguruje parametry i historię; test wpływu na akumulator.
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
| M2 API wielu adapterów | US-20,21,14,16,17 | `poll_data(adapter_id, metrics)` z lokalną kolejką, deduplikacją i wynikiem do HA |
| M3 Automatyczny MVP OBD | US-12,13,22,23,24,18 | Vgate BLE uruchamia tylko jedną lokalną ograniczoną sesję, `0131/0101/03/07`, wyniki do HA |
| M4 Rozszerzenia | US-03,06,08,09,15 | Identyfikacja tagów, trendy i eksport historii w HA, prosty panel konfiguracji |
| M5 Rozszerzona diagnostyka | US-19 | Interpretacja, Mode 0A/02 i opcjonalne moduły producenta; podstawowe DTC 03/07 są już w M3 |

US-14 jest **bramką** dla automatycznych odczytów; US-16/17 są obowiązkowe przy każdej implementacji. US-20/21/22/23/24 są wymagane dla MVP OBD; US-19 dotyczy wyłącznie późniejszych rozszerzeń interpretacji/diagnostyki.

## Obserwacje terenowe — Nissan Qashqai / Vgate iCar Pro BLE (2026-11-18)

**Konfiguracja testu:** Nissan Qashqai J10, adapter Vgate iCar Pro BLE (nazwa `IOS-Vlink`, MAC `D2:E0:2F:8D:52:48`), ESP32 OBD2MQTT v0.3.24; ręczne „Scan for dongles”. `Polling: OFF`.

| Okoliczność | Wynik zaobserwowany |
|---|---|
| Zapłon włączony | Vgate widoczny przez BLE; wcześniej potwierdzono odpowiedzi ECU na PID-y |
| Około 1 min po wyjęciu kluczyka | Adapter nadal rozgłasza BLE, ECU zwraca `NO DATA` |
| Około 60 min po wyjęciu kluczyka | Vgate nieobecny w wynikach jednego ręcznego skanu; inne urządzenia były wykrywane |
| Otwarcie drzwi samochodu, bez wkładania kluczyka i bez włączania zapłonu | Adapter ponownie wykrywalny przez BLE, ale PID-y zwracają `NO DATA` — test użytkownika |

**Interpretacja i ograniczenia:** zaobserwowano uśpienie i wybudzenie Vgate; pojedynczy brak reklamy nie dowodzi wyjazdu, ponowne reklamy po otwarciu drzwi nie dowodzą gotowości ECU. Poprzednie próby HA co 30 s/10 min były **historyczną koncepcją v0.2** i zostały zastąpione w refinement v0.3 lokalnym retry w ESP; testy faktycznego zachowania ECU oraz poboru prądu pozostają wymagane.

**Konsekwencje dla stories:** US-04/05/11 zachowują trójstanowe BLE bez etykiet arrival/departure; US-12/13/22 w v0.3 przewidują opt-in trigger Vgate i lokalne retry ESP, a nie pętlę HA; US-14/18 obejmują bezpieczne limity i testy postoju.

## Decyzje otwarte (nie rozstrzygać przed pomiarami)

- Interwał/okna skanowania i budżet radiowy przy Wi-Fi + NimBLE + ELM.
- Liczba urządzeń i retencja próbek przy dostępnej pamięci ESP32-WROOM-32D.
- Okna potwierdzania, agregacja RSSI, filtr trendu i klasyfikacja danych niewystarczających.
- Mechanizmy identyfikacji tagów o zmiennych adresach.
- Format eksportu logów i szczegóły discovery/availability HA.
- Zasady małej kolejki/odrzucania zadań przy wielu adapterach, graniczny wiek wiadomości MQTT, deduplikacja i timeout. Istniejąca v0.3.24 używa jednej konfiguracji BLE/profilu, więc wymaga refaktoryzacji.
- Weryfikacja parametrów startowych ESP (600/3/5/8/2/180/300 s) na logach, w szczególności okna skanowania NimBLE (M1 obecnie 4 s co 30 s), blokad BLE/ELM, wieloramkowych DTC, czasu ECU, WDT, pamięci i poboru prądu. To wartości do tuningu, nie zweryfikowane nastawy.
- Analiza po testach uśpienia Vgate: BLE nieobecny może oznaczać sleep, nie oddalenie.
- US-19 (późniejszy etap): zakres Mode 03/07/0A i Mode 02 dla konkretnych ECU, format surowych ramek, źródła opisów DTC, reguły ocen pilności, polityka lokalnego/ewentualnie zewnętrznego wyjaśniania usterek oraz kontrakt `read_dtc(adapter_id)`.

## Zweryfikowane punkty wejścia aktualnego kodu

- `src/elm_ble.cpp`: `ElmBle::scan(uint32_t ms)`, callbacki NimBLE, adres i RSSI.
- `src/poller.cpp`: `Poller::requestPollNow()`, kolejka zadań, okresowe polle i backoff.
- `src/web.cpp`: `POST /api/pollnow`.
- `src/mqtt_ha.cpp`: publikacja MQTT i Discovery.
- `README.md`: obecny zakres firmware i opis oszczędzania energii.

Żadne z powyższych nie oznacza, że nowa funkcja jest już zaimplementowana.
