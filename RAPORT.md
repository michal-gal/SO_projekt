# Raport – SO Projekt: „Restauracja” (C / Linux / System V IPC)

Repo: https://github.com/michal-gal/SO_projekt  
Commit (linki w raporcie są „zamrożone” na ten stan): `527023cba2f88e41ac279f405238f7e3e8860a0b`

## Założenia projektowe

- Symulacja restauracji w modelu **wieloprocesowym**: proces nadrzędny uruchamia procesy obsługi, kucharza, kierownika oraz wiele procesów klientów.
- Współdzielenie stanu przez **System V IPC**: pamięć dzielona + semafory; kolejka komunikatów jako kolejka wejściowa klientów.
- Program ma poprawnie reagować na przerwania z terminala (Ctrl+C/Ctrl+\) oraz umożliwiać stop/wznów (Ctrl+Z/fg) i domykać zasoby IPC.
- Logi „gadane” mają być łatwe do wyciszenia i nie mogą być wykonywane w sekcjach krytycznych (pod semaforami / w newralgicznych pętlach IPC).

## Ogólny opis kodu

- `restauracja.c`: główny proces – tworzy IPC, uruchamia dzieci przez fork/exec, zarządza grupą procesów, forwarduje sygnały job-control, na końcu sprząta IPC.
- `common.c`: wspólne API dla IPC (shm/semafory/msgq), generator stolików, helpery synchronizacji i porządkowania.
- `klient.c` / `obsluga.c` / `kucharz.c` / `kierownik.c`: logika ról + łagodna obsługa SIGTERM.
- `log.c` + `log.h`: wspólny logger (`LOGI/LOGD/LOGE`), poziomy logowania, zapis do pliku, prefiks czasu/PID/poziomu.

## Co udało się zrobić

- Uporządkowana obsługa sygnałów i job-control: forwardowanie SIGINT/SIGQUIT/SIGTSTP/SIGCONT do grupy dzieci, a zamykanie przez `kill(-pgid, ...)`.
- Ujednolicony shutdown: wspólny helper czyszczący stoliki/kolejkę + kończenie klientów, używany w ścieżce zamknięcia.
- Refaktor logów: usunięcie wypisywania z sekcji krytycznych i pętli IPC; dodany przełącznik `LOG_LEVEL`.
- Logger do pliku: logowanie do pliku przez env, możliwość wyłączenia stdout/stderr, prefiks (timestamp + pid + level).

## Problemy napotkane w trakcie

- Job-control: dzieci są w osobnej grupie procesów, więc bez forwardowania sygnałów zachowanie Ctrl+Z/Ctrl+C było nieintuicyjne.
- Logowanie w sekcjach krytycznych pogarszało współbieżność i mogło prowadzić do trudnych zacięć (I/O + semafory).
- Makra logowania: łatwo „połamać” multiline-macro — finalnie logowanie jest scentralizowane w `log.h/log.c`.

## Dodane elementy specjalne

- Jedna grupa procesów potomnych i domykanie przez `kill(-pgid, SIGTERM/SIGKILL)`.
- Przełącznik głośności logów (`LOG_LEVEL`) + log do pliku z prefiksem.
- Spójny reporting błędów przez `LOGE/LOGE_ERRNO`.

## Zauważone problemy z testami

- Brak automatycznych testów jednostkowych/integracyjnych; weryfikacja głównie „smoke run” (uruchomienie na krótko i obserwacja).
- Symulacja jest losowa/czasowa, więc wyniki są słabo deterministyczne; do twardych testów przydałby się tryb deterministyczny (np. seed podawany parametrem).

---

## Linki do istotnych fragmentów kodu (GitHub)

Poniżej linki do miejsc w źródłach, które obrazują wymagane konstrukcje / funkcje systemowe.

### a) Tworzenie i obsługa plików (creat(), open(), close(), read(), write(), unlink())

- `open()` + `close()` + `atexit()` (logger):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/log.c#L14-L45
- `write()` (zapis do pliku i na stdout/stderr):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/log.c#L116-L123
- W projekcie nie użyto: `creat()`, `read()`, `unlink()`:
  - creat: https://github.com/michal-gal/SO_projekt/search?q=creat%28&type=code
  - read: https://github.com/michal-gal/SO_projekt/search?q=read%28&type=code
  - unlink: https://github.com/michal-gal/SO_projekt/search?q=unlink%28&type=code

### b) Tworzenie procesów (fork(), exec(), exit(), wait())

- `fork()` + `execl()` + `_exit()`:
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/restauracja.c#L76-L92
- `waitpid()` (zbieranie zombie):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/restauracja.c#L66-L74
- `waitpid()` (końcowe domykanie):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/restauracja.c#L124-L131
- `exit()` (przykład):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/kierownik.c#L83-L86

### c) Tworzenie i obsługa wątków (pthread\_\*)

- W projekcie nie użyto wątków (brak `pthread_*`):
  - https://github.com/michal-gal/SO_projekt/search?q=pthread_create&type=code

### d) Obsługa sygnałów (kill(), raise(), signal(), sigaction())

- `signal()` (podpięcie handlerów job-control):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/restauracja.c#L162-L169
- `kill()` (forward i job-control):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/restauracja.c#L37-L63
- `kill()` (SIGUSR1/SIGUSR2 do obsługi):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/kierownik.c#L37-L52
- W projekcie nie użyto: `raise()`, `sigaction()`:
  - raise: https://github.com/michal-gal/SO_projekt/search?q=raise%28&type=code
  - sigaction: https://github.com/michal-gal/SO_projekt/search?q=sigaction%28&type=code

### e) Synchronizacja procesów/wątków (ftok(), semget(), semctl(), semop())

- `semop()`:
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/common.c#L141-L160
- `semget()` + `semctl()` (tworzenie/inicjalizacja):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/common.c#L186-L191
- W projekcie nie użyto `ftok()` (użyto `IPC_PRIVATE`):
  - https://github.com/michal-gal/SO_projekt/search?q=ftok%28&type=code

### f) Łącza nazwane i nienazwane (mkfifo(), pipe(), dup(), dup2(), popen())

- W projekcie nie użyto: `mkfifo()`, `pipe()`, `dup()`, `dup2()`, `popen()`:
  - https://github.com/michal-gal/SO_projekt/search?q=mkfifo%28%7Cpipe%28%7Cdup%28%7Cdup2%28%7Cpopen%28&type=code

### g) Segmenty pamięci dzielonej (ftok(), shmget(), shmat(), shmdt(), shmctl())

- `shmget()` + `shmat()`:
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/common.c#L167-L176
- `shmctl()` (sprzątanie):
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/restauracja.c#L226-L230
- W projekcie nie użyto: `shmdt()`, `ftok()`:
  - shmdt: https://github.com/michal-gal/SO_projekt/search?q=shmdt%28&type=code
  - ftok: https://github.com/michal-gal/SO_projekt/search?q=ftok%28&type=code

### h) Kolejki komunikatów (ftok(), msgget(), msgsnd(), msgrcv(), msgctl())

- `msgget()`:
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/common.c#L192-L197
- `msgsnd()`:
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/common.c#L231-L249
- `msgrcv()`:
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/common.c#L255-L271
- `msgctl()`:
  - https://github.com/michal-gal/SO_projekt/blob/527023cba2f88e41ac279f405238f7e3e8860a0b/restauracja.c#L226-L230
- W projekcie nie użyto `ftok()`:
  - https://github.com/michal-gal/SO_projekt/search?q=ftok%28&type=code

### i) Gniazda (socket(), bind(), listen(), accept(), connect())

- W projekcie nie użyto gniazd:
  - https://github.com/michal-gal/SO_projekt/search?q=socket%28%7Cbind%28%7Clisten%28%7Caccept%28%7Cconnect%28&type=code
