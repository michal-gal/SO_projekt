# Raport – SO Projekt: „Restauracja” (C / Linux / System V IPC)

Repozytorium: https://github.com/michal-gal/SO_projekt  
Commit (linki w raporcie są „zamrożone” na ten stan): `527023cba2f88e41ac279f405238f7e3e8860a0b`

## Specyfikacja kompilacji

Projekt był kompilowany i uruchamiany na następującej konfiguracji:

- System: Linux 6.17.0-8-generic (x86_64)
- Kompilator: gcc (Ubuntu 15.2.0-4ubuntu4) 15.2.0
- Narzędzia budowania: GNU Make 4.4.1
- Kompilacja: `make clean && make`

## Założenia projektowe

- Symulacja restauracji w modelu **wieloprocesowym**: proces nadrzędny uruchamia procesy obsługi, kucharza, kierownika oraz wiele procesów klientów.
- Współdzielenie stanu przez **System V IPC**: pamięć dzielona + semafory; kolejka komunikatów jako kolejka wejściowa klientów.
- Program ma poprawnie reagować na przerwania z terminala (Ctrl+C/Ctrl+\) oraz umożliwiać stop/wznów (Ctrl+Z/fg) i domykać zasoby IPC.
- Logi wyświetlane na konsoli są łatwe do wyciszenia i nie mogą być wykonywane w sekcjach krytycznych (pod semaforami / w newralgicznych pętlach IPC).

## Ogólny opis kodu

- `restauracja.c`: główny proces – tworzy IPC, uruchamia dzieci przez fork/exec, zarządza grupą procesów, forwarduje sygnały job-control, na końcu sprząta IPC.
- `common.c`: wspólne API dla IPC (shm/semafory/msgq), generator stolików, funkcje pomocnicze synchronizacji i porządkowania.
- `klient.c` / `obsluga.c` / `kucharz.c` / `kierownik.c`: logika ról + łagodna obsługa SIGTERM.
- `log.c` + `log.h`: wspólny logger (`LOGI/LOGD/LOGE`), poziomy logowania, zapis do pliku, prefiks czasu/PID/poziomu.

## Dodane elementy specjalne

- Jedna grupa procesów potomnych i domykanie przez `kill(-pgid, SIGTERM/SIGKILL)`.
- Przełącznik głośności logów (`LOG_LEVEL`) + log do pliku z prefiksem.
- Spójne raportowanie błędów przez `LOGE/LOGE_ERRNO`.
- Ochrona kolejki wejściowej klientów przed zapchaniem: dodatkowy semafor zliczający `SEM_KOLEJKA` (limit `MAX_KOLEJKA_MSG`) działa jak „liczba wolnych slotów” w kolejce komunikatów. Klient rezerwuje slot przed `msgsnd()`, a obsługa zwalnia slot po `msgrcv()`, dzięki czemu przy dużej liczbie klientów procesy blokują się na semaforze zamiast przepełniać msgqueue.

## Testy (opis i wynik)

W repozytorium znajduje się zestaw prostych testów w `tests/` (bash), które budują projekt i sprawdzają poprawne domykanie procesu nadrzędnego, potomków oraz zasobów IPC.

Uruchamianie testów:

- Wszystkie testy: `make test` (jeśli cel jest dostępny) albo ręcznie: `bash tests/test_*.sh`
- Pojedynczy test: np. `bash tests/test_signals.sh`

Każdy test uruchamia `./restauracja` w kontrolowany sposób (z limitami czasu) i sprawdza wybrane własności (np. reakcję na sygnały, brak osieroconych procesów, poprawne sprzątanie IPC). Część testów może korzystać ze zmiennych środowiskowych, np. `RESTAURACJA_DISABLE_MANAGER_CLOSE=1` lub wariantu kompilacji `-DTEST_NO_SLEEP`.

Podsumowanie: wszystkie poniższe testy kończą się wynikiem **OK**.

### `tests/test_smoke.sh` – test bazowy

- Cel: szybka weryfikacja, że projekt się buduje i uruchamia bez awarii.
- Przebieg: `make clean && make`, następnie uruchomienie `./restauracja` z limitem czasu (ok. 3 s) i logowaniem do pliku.
- Kryterium zaliczenia: program nie kończy się błędem i powstaje niepusty plik logów.
- Wynik: **OK**.

### `tests/test_signals.sh` – test obsługi sygnałów

- Cel: sprawdzenie kontrolowanego zamknięcia po sygnale `SIGINT` (Ctrl+C) wysłanym do procesu rodzica.
- Przebieg: uruchomienie `./restauracja` w tle, wysłanie `SIGINT`, oczekiwanie na zakończenie w limicie czasu.
- Kryterium zaliczenia: proces kończy się w limicie, log powstaje (i nie jest pusty).
- Wynik: **OK**.

### `tests/test_jobcontrol.sh` – test job-control

- Cel: weryfikacja poprawnej reakcji na `SIGTSTP` (stop), `SIGCONT` (wznów) i finalne zamknięcie `SIGINT`.
- Przebieg: start `./restauracja` → stop (`SIGTSTP`) → wznów (`SIGCONT`) → zamknięcie (`SIGINT`), z oczekiwaniem na zakończenie.
- Kryterium zaliczenia: program kończy się kontrolowanie w limicie czasu (bez zawieszenia), log powstaje.
- Wynik: **OK**.

### `tests/test_no_orphans.sh` – test „braku sierot”

- Cel: upewnienie się, że po zakończeniu nie zostają procesy potomne (klienci/role) działające w tle.
- Przebieg: uruchomienie `./restauracja` (opcjonalnie przez `setsid`), wysłanie `SIGINT`, a następnie sprawdzenie listy procesów.
- Kryterium zaliczenia: po zakończeniu brak uruchomionych procesów `restauracja/obsluga/kucharz/kierownik/klient` powiązanych z sesją testu.
- Wynik: **OK**.

### `tests/test_no_sleep.sh` – test bez opóźnień (`TEST_NO_SLEEP`)

- Cel: uruchomienie programu w wariancie testowym, w którym `sleep/nanosleep` nie blokują, aby szybciej wykrywać deadlocki i błędy sterowania.
- Przebieg: kompilacja z `EXTRA_CFLAGS='-DTEST_NO_SLEEP'`, uruchomienie z krótkim czasem pracy (`RESTAURACJA_CZAS_PRACY`) oraz zablokowanym losowym zamknięciem przez kierownika (`RESTAURACJA_DISABLE_MANAGER_CLOSE=1`).
- Kryterium zaliczenia: program kończy się sam w zadanym czasie i tworzy niepusty log.
- Wynik: **OK**.
- Uwagi: nie tworzą się procesy klientów.

## Linki do istotnych fragmentów kodu (GitHub)

Poniżej linki do miejsc w źródłach, które obrazują wymagane konstrukcje / funkcje systemowe.

### a) Tworzenie i obsługa plików (creat(), open(), close(), read(), write(), unlink())

- `open()` + `close()` + `atexit()` (logger):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/log.c#L14-L45
- `write()` (zapis do pliku i na stdout/stderr):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/log.c#L116-L123

### b) Tworzenie procesów (fork(), exec(), exit(), wait())

- `fork()` + `execl()` + `_exit()`:
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/restauracja.c#L76-L92
- `waitpid()` (zbieranie zombie):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/restauracja.c#L66-L74
- `waitpid()` (końcowe domykanie):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/restauracja.c#L124-L131
- `exit()` (przykład):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/kierownik.c#L83-L86

### c) Obsługa sygnałów (kill(), raise(), signal(), sigaction())

- `signal()` (podpięcie handlerów job-control):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/restauracja.c#L162-L169
- `kill()` (forward i job-control):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/restauracja.c#L37-L63
- `kill()` (SIGUSR1/SIGUSR2 do obsługi):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/kierownik.c#L37-L52

### d) Synchronizacja procesów/wątków (ftok(), semget(), semctl(), semop())

- `semop()`:
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/common.c#L141-L160
- `semget()` + `semctl()` (tworzenie/inicjalizacja):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/common.c#L186-L191

### e) Segmenty pamięci dzielonej (ftok(), shmget(), shmat(), shmdt(), shmctl())

- `shmget()` + `shmat()`:
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/common.c#L167-L176
- `shmctl()` (sprzątanie):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/restauracja.c#L226-L230

### f) Kolejki komunikatów (ftok(), msgget(), msgsnd(), msgrcv(), msgctl())

Dodatkowo zastosowano semafor pojemności kolejki (`SEM_KOLEJKA`), aby ograniczyć liczbę komunikatów oczekujących w msgqueue i uniknąć sytuacji, w której masowe uruchomienie klientów zapełnia kolejkę.

- `msgget()`:
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/common.c#L192-L197
- `msgsnd()`:
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/common.c#L231-L249
- `msgrcv()`:
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/common.c#L255-L271
- `msgctl()`:
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/restauracja.c#L226-L230
