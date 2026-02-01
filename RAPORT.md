# Raport – SO Projekt: „Restauracja” (C / Linux / System V IPC)

Repozytorium: https://github.com/michal-gal/SO_projekt

## Specyfikacja kompilacji

Projekt był kompilowany i uruchamiany na następującej konfiguracji:

- System: Linux 6.17.0-8-generic (x86_64)
- Kompilator: gcc (Ubuntu 15.2.0-4ubuntu4) 15.2.0
- Narzędzia budowania: GNU Make 4.4.1
- Kompilacja: `make clean && make`

Uruchamianie programu:

- `./build/bin/restauracja [<liczba_klientow>] [<czas_sekund>] [<log_level>]`
- Domyślne wartości (gdy nie podano argumentów): 5000 klientów, 10 sekund, log level 1.

## Założenia projektowe

- Symulacja restauracji w modelu **wieloprocesowym**: proces nadrzędny uruchamia procesy obsługi, kucharza, kierownika oraz wiele procesów klientów.
- Współdzielenie stanu przez **System V IPC**: pamięć dzielona + semafory; kolejka komunikatów jako kolejka wejściowa klientów.
- Program ma poprawnie reagować na przerwania z terminala (Ctrl+C / Ctrl+\), umożliwiać stop/wznów (SIGTSTP/SIGCONT) i sprzątać zasoby IPC.
- Logi na konsoli można łatwo wyciszyć; logowanie nie odbywa się w sekcjach krytycznych (pod semaforami / w newralgicznych pętlach IPC).

## Ogólny opis kodu

- `restauracja.c`: główny proces – tworzy IPC, uruchamia dzieci przez fork/exec, zarządza grupą procesów, forwarduje sygnały job-control, na końcu sprząta IPC.
- `common.c`: wspólne API dla IPC (shm/semafory/msgq), generator stolików, funkcje pomocnicze synchronizacji i sprzątania.
- `klient.c` / `obsluga.c` / `kucharz.c` / `kierownik.c`: logika ról + łagodna obsługa SIGTERM.
- `log.c` + `log.h`: wspólny logger (`LOGI/LOGD/LOGE/LOGS/LOGP`), poziomy logowania 0..3, zapis do pliku, prefiks czasu/PID/poziomu.

## Dodane elementy specjalne

- Jedna grupa procesów potomnych i domykanie przez `kill(-pgid, SIGTERM/SIGKILL)`.
- Przełącznik głośności logów (`LOG_LEVEL`) + log do pliku z prefiksem.
- Spójne raportowanie błędów przez `LOGE/LOGE_ERRNO`.
- Kolejka wejściowa klientów jest ograniczana przez licznik i warunki w `queue_sync` (mutex/cond) zamiast dodatkowego semafora.

## Testy (opis i wynik)

W repozytorium znajduje się zestaw prostych testów w `tests/` (bash), które budują projekt i sprawdzają poprawne domykanie procesu nadrzędnego, potomków oraz zasobów IPC.

Uruchamianie testów:

- Wszystkie testy: `make test` lub ręcznie: `bash tests/test_*.sh`
- Pojedynczy test: np. `bash tests/test_signals.sh`

Każdy test uruchamia `./restauracja` w kontrolowany sposób (z limitami czasu) i sprawdza wybrane własności (np. reakcję na sygnały, brak osieroconych procesów, poprawne sprzątanie IPC). Część testów może korzystać ze zmiennych środowiskowych, np. `RESTAURACJA_DISABLE_MANAGER_CLOSE=1`.

Podsumowanie: wszystkie poniższe testy kończą się wynikiem **OK**.

### `tests/test_smoke.sh` – test bazowy

- Cel: szybka weryfikacja, że projekt się buduje i uruchamia bez awarii.
- Przebieg: `make clean && make`, następnie uruchomienie `./build/bin/restauracja` z limitem czasu (ok. 3 s) i logowaniem do pliku.
- Kryterium zaliczenia: program nie kończy się błędem i powstaje niepusty plik logów.
- Wynik: **OK**.

### `tests/test_signals.sh` – test obsługi sygnałów

- Cel: sprawdzenie kontrolowanego zamknięcia po sygnale `SIGINT` (Ctrl+C) wysłanym do procesu rodzica.
- Przebieg: uruchomienie `./build/bin/restauracja` w tle, wysłanie `SIGINT`, oczekiwanie na zakończenie w limicie czasu.
- Kryterium zaliczenia: proces kończy się w limicie, log powstaje (i nie jest pusty).
- Wynik: **OK**.

### `tests/test_jobcontrol.sh` – test job-control

- Cel: weryfikacja poprawnej reakcji na `SIGTSTP` (stop), `SIGCONT` (wznów) i finalne zamknięcie `SIGINT`.
- Przebieg: start `./build/bin/restauracja` → stop (`SIGTSTP`) → wznów (`SIGCONT`) → zamknięcie (`SIGINT`), z oczekiwaniem na zakończenie.
- Kryterium zaliczenia: program kończy się kontrolowanie w limicie czasu (bez zawieszenia), log powstaje.
- Wynik: **OK**.

### `tests/test_no_orphans.sh` – test „braku sierot”

- Cel: upewnienie się, że po zakończeniu nie zostają procesy potomne (klienci/role) działające w tle.
- Przebieg: uruchomienie `./build/bin/restauracja` (opcjonalnie przez `setsid`), wysłanie `SIGINT`, a następnie sprawdzenie listy procesów.
- Kryterium zaliczenia: po zakończeniu brak uruchomionych procesów `restauracja/obsluga/kucharz/kierownik/klient` powiązanych z sesją testu.
- Wynik: **OK**.

## Linki do istotnych fragmentów kodu

Poniżej wskazania miejsc w kodzie, które obrazują wymagane konstrukcje i funkcje systemowe.
Ze względu na brak przypiętego commita w raporcie, podaję konkretne pliki i funkcje.

### a) Tworzenie i obsługa plików (creat(), open(), close(), read(), write(), unlink())

- `open()` + `close()` + `atexit()` (logger): `log.c` → `inicjuj_log_raz()`, `zamknij_log_przy_wyjsciu()`
- `write()` (zapis do pliku i na stdout/stderr): `log.c` → `loguj_vprintf()`, `loguj_blokiem()`

### b) Tworzenie procesów (fork(), exec(), exit(), wait())

- `fork()` + `execl()` + `_exit()`: `restauracja.c` → `uruchom_potomka_exec()`
- `waitpid()` (zbieranie zombie): `restauracja.c` → `zbierz_zombie_nieblokujaco()`
- `waitpid()` (końcowe domykanie): `restauracja.c` → `zakoncz_wszystkie_dzieci()`
- `exit()` (przykład): `kierownik.c` → `kierownik()`

### c) Obsługa sygnałów (kill(), raise(), signal(), sigaction())

- `signal()` (podpięcie handlerów job-control): `restauracja.c` → `inicjuj_restauracje()`
- `kill()` (forward i job-control): `restauracja.c` → `obsluz_sygnal_restauracji()`
- `kill()` (SIGUSR1/SIGUSR2 do obsługi): `kierownik.c` → `kierownik_wyslij_sygnal_obsludze()`

### d) Synchronizacja procesów/wątków (ftok(), semget(), semctl(), semop())

- `semop()` (operacje na semaforach): `common.c` → `sem_operacja()`
- `semget()` + `semctl()` (tworzenie/inicjalizacja): `common.c` → `inicjuj_semafory()`

### e) Segmenty pamięci dzielonej (ftok(), shmget(), shmat(), shmdt(), shmctl())

- `shmget()` + `shmat()` (tworzenie/dolaczanie): `common.c` → `stworz_ipc()`
- `shmctl()` (sprzątanie): `restauracja.c` → `zamknij_restauracje()`

### f) Kolejki komunikatów (ftok(), msgget(), msgsnd(), msgrcv(), msgctl())

- `msgget()` (tworzenie): `common.c` → `stworz_ipc()`
- `msgsnd()` (wysyłka): `klient.c` → `kolejka_dodaj()` / `kolejka_dodaj_local()`
- `msgrcv()` (odbiór): `szatnia.c` → `kolejka_pobierz_local()`
- `msgctl()` (sprzątanie): `restauracja.c` → `zamknij_restauracje()`
