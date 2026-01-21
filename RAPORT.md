# Raport – SO Projekt: „Restauracja” (C / Linux / System V IPC)

Repo: https://github.com/michal-gal/SO_projekt  
Commit (linki w raporcie są „zamrożone” na ten stan): `527023cba2f88e41ac279f405238f7e3e8860a0b`

## Założenia projektowe

- Symulacja restauracji w modelu **wieloprocesowym**: proces nadrzędny uruchamia procesy obsługi, kucharza, kierownika oraz wiele procesów klientów.
- Współdzielenie stanu przez **System V IPC**: pamięć dzielona + semafory; kolejka komunikatów jako kolejka wejściowa klientów.
- Program ma poprawnie reagować na przerwania z terminala (Ctrl+C/Ctrl+\) oraz umożliwiać stop/wznów (Ctrl+Z/fg) i domykać zasoby IPC.
- Logi wyświetlane na konsolisą łatwe do wyciszenia i nie mogą być wykonywane w sekcjach krytycznych (pod semaforami / w newralgicznych pętlach IPC).

## Ogólny opis kodu

- `restauracja.c`: główny proces – tworzy IPC, uruchamia dzieci przez fork/exec, zarządza grupą procesów, forwarduje sygnały job-control, na końcu sprząta IPC.
- `common.c`: wspólne API dla IPC (shm/semafory/msgq), generator stolików, helpery synchronizacji i porządkowania.
- `klient.c` / `obsluga.c` / `kucharz.c` / `kierownik.c`: logika ról + łagodna obsługa SIGTERM.
- `log.c` + `log.h`: wspólny logger (`LOGI/LOGD/LOGE`), poziomy logowania, zapis do pliku, prefiks czasu/PID/poziomu.

## Dodane elementy specjalne

- Jedna grupa procesów potomnych i domykanie przez `kill(-pgid, SIGTERM/SIGKILL)`.
- Przełącznik głośności logów (`LOG_LEVEL`) + log do pliku z prefiksem.
- Spójny reporting błędów przez `LOGE/LOGE_ERRNO`.
- Ochrona kolejki wejściowej klientów przed zapchaniem: dodatkowy semafor zliczający `SEM_KOLEJKA` (limit `MAX_KOLEJKA_MSG`) działa jak „liczba wolnych slotów” w kolejce komunikatów. Klient rezerwuje slot przed `msgsnd()`, a obsługa zwalnia slot po `msgrcv()`, dzięki czemu przy dużej liczbie klientów procesy blokują się na semaforze zamiast przepełniać msgqueue.

## Zauważone problemy z testami

## Testy (opis i wynik)

W repozytorium znajduje się zestaw prostych testów w `tests/` (bash), które budują projekt i sprawdzają poprawne domykanie procesu nadrzędnego, potomków oraz zasobów IPC.

- `tests/test_smoke.sh` – „dymny” test: `make clean && make`, uruchomienie `./restauracja` z krótkim czasem pracy i logowaniem do pliku; test przechodzi, jeśli program nie wykrzacza się oraz powstaje niepusty plik logów. **Wynik: OK**.
- `tests/test_signals.sh` – test sygnałów: start `./restauracja` w tle, wysłanie `SIGINT` do procesu rodzica i oczekiwanie na kontrolowane zakończenie w limicie czasu; dodatkowo weryfikacja, że log powstał. **Wynik: OK**.
- `tests/test_jobcontrol.sh` – test job-control: `SIGTSTP` (stop) → `SIGCONT` (wznów) → `SIGINT` (zamknięcie) i oczekiwanie na koniec w limicie czasu; log musi powstać. **Wynik: OK**.
- `tests/test_no_orphans.sh` – test „braku sierot”: uruchomienie (opcjonalnie przez `setsid`), `SIGINT` do rodzica, a następnie sprawdzenie, że po zakończeniu nie ma już uruchomionych procesów `restauracja/obsluga/kucharz/kierownik/klient` powiązanych z sesją testu. **Wynik: OK**.
- `tests/test_no_sleep.sh` – test bez opóźnień: budowa z `-DTEST_NO_SLEEP` (sleep/nanosleep nie blokują), uruchomienie z ustawionym czasem pracy i zablokowanym zamykaniem przez kierownika (`RESTAURACJA_DISABLE_MANAGER_CLOSE=1`), weryfikacja że powstaje log. **Wynik: OK**.

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

### d) Obsługa sygnałów (kill(), raise(), signal(), sigaction())

- `signal()` (podpięcie handlerów job-control):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/restauracja.c#L162-L169
- `kill()` (forward i job-control):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/restauracja.c#L37-L63
- `kill()` (SIGUSR1/SIGUSR2 do obsługi):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/kierownik.c#L37-L52

### e) Synchronizacja procesów/wątków (ftok(), semget(), semctl(), semop())

- `semop()`:
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/common.c#L141-L160
- `semget()` + `semctl()` (tworzenie/inicjalizacja):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/common.c#L186-L191

### g) Segmenty pamięci dzielonej (ftok(), shmget(), shmat(), shmdt(), shmctl())

- `shmget()` + `shmat()`:
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/common.c#L167-L176
- `shmctl()` (sprzątanie):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/restauracja.c#L226-L230

### h) Kolejki komunikatów (ftok(), msgget(), msgsnd(), msgrcv(), msgctl())

Dodatkowo zastosowano semafor pojemności kolejki (`SEM_KOLEJKA`), aby ograniczyć liczbę komunikatów oczekujących w msgqueue i uniknąć sytuacji, w której masowe uruchomienie klientów zapełnia kolejkę.

- `msgget()`:
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/common.c#L192-L197
- `msgsnd()`:
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/common.c#L231-L249
- `msgrcv()`:
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/common.c#L255-L271
- `msgctl()`:
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/restauracja.c#L226-L230
