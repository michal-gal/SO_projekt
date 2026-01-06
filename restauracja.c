#include "restauracja.h"
#include <errno.h>

// ====== ZMIENNE GLOBALNE (DEFINICJE) ======
int shm_id, sem_id;        // ID pamięci współdzielonej i semaforów
struct Kolejka *kolejka;   // wskaźnik na kolejkę
struct Stolik *stoliki;    // wskaźnik na tablicę stolików
int *sygnal_kierownika;    // wskaźnik na sygnał kierownika
int *restauracja_otwarta;  // wskaźnik na stan restauracji
int *aktywni_klienci;      // wskaźnik na liczbę aktywnych klientów
int *kuchnia_dania_wydane; // liczba wydanych dań przez kuchnię
int *kasa_dania_sprzedane; // liczba sprzedanych dań przez kasę
int *tasma;                // tablica reprezentująca taśmę

// ====== MAIN ======
int main()
{

    srand(time(NULL));
    int bufor = sizeof(struct Kolejka) +              // kolejka
                sizeof(struct Stolik) * MAX_STOLIKI + // stoliki
                sizeof(int) * MAX_TASMA +             // taśma
                sizeof(int) * 6 * 2 +                 // kuchnia i kasa - liczba dań
                sizeof(int) * 3 +                     // sygnał kierownika i stan restauracji, i aktywni klienci
                sizeof(int) * 6;                      // pobrane dania

    shm_id = shmget(IPC_PRIVATE, bufor, IPC_CREAT | 0666); // utworzenie pamięci współdzielonej
    void *pamiec_wspoldzielona = shmat(shm_id, NULL, 0);   // dołączenie pamięci współdzielonej
    memset(pamiec_wspoldzielona, 0, bufor);                // wyzerowanie pamięci

    kolejka = pamiec_wspoldzielona;
    stoliki = (void *)(kolejka + 1);
    tasma = (int *)(stoliki + MAX_STOLIKI);
    kuchnia_dania_wydane = (int *)(tasma + MAX_TASMA);
    kasa_dania_sprzedane = kuchnia_dania_wydane + 6;
    sygnal_kierownika = kasa_dania_sprzedane + 6;
    restauracja_otwarta = sygnal_kierownika + 1;
    aktywni_klienci = restauracja_otwarta + 1;

    sem_id = semget(IPC_PRIVATE, 3, IPC_CREAT | 0666); // utworzenie zestawu semaforów
    semctl(sem_id, SEM_KOLEJKA, SETVAL, 1);            // inicjalizacja semaforów
    semctl(sem_id, SEM_STOLIKI, SETVAL, 1);
    semctl(sem_id, SEM_TASMA, SETVAL, 1);

    generator_stolikow(stoliki);
    fflush(stdout); // opróżnij bufor przed fork() aby uniknąć duplikatów

    *restauracja_otwarta = 1;

    pid_t pid_obsluga, pid_kucharz, pid_kierownik, pid_generator;

    pid_obsluga = fork();
    if (pid_obsluga == 0)
    {
        obsluga();
    }
    else if (pid_obsluga == -1)
    {
        perror("fork failed");
        exit(1);
    }

    pid_kucharz = fork();
    if (pid_kucharz == 0)
    {
        kucharz();
    }
    else if (pid_kucharz == -1)
    {
        perror("fork failed");
        exit(1);
    }

    pid_kierownik = fork();
    if (pid_kierownik == 0)
    {
        kierownik();
    }
    else if (pid_kierownik == -1)
    {
        perror("fork failed");
        exit(1);
    }

    pid_generator = fork();
    if (pid_generator == 0)
    {
        generator_klientow();
    }
    else if (pid_generator == -1)
    {
        perror("fork failed");
        exit(1);
    }

    sleep(CZAS_PRACY);
    *restauracja_otwarta = 0;
    printf("\n===Czas pracy restauracji minął!===\n");
    fflush(stdout); // upewnij się że komunikaty zostały wydrukowane

    printf("Zamykanie restauracji...\n");
    // Daj chwilę na naturalne zakończenie klientów zanim zamkniemy procesy główne
    time_t koniec_klienci_start = time(NULL);
    while (*aktywni_klienci > 0 && time(NULL) - koniec_klienci_start < 5) // czekaj maksymalnie 5 sekund
    {
        printf("Czekam na klientów: %d\n", *aktywni_klienci);
        fflush(stdout);
        usleep(200000); // 200 ms
    }

    sleep(1);       // dodatkowy mały bufor
    fflush(stdout); // upewnij się że komunikaty zostały wydrukowane

    // Czekanie na zakończenie wszystkich procesów potomnych z timeoutem
    time_t czas_start = time(NULL);
    pid_t reaped;
    int status;
    int licznik_procesow = 0;

    // Czekamy na 4 główne procesy (obsluga, kucharz, kierownik, generator)
    while (licznik_procesow < 4)
    {
        reaped = waitpid(-1, &status, WNOHANG);

        if (reaped > 0)
        {
            licznik_procesow++;
            const char *nazwa = "nieznany";
            if (reaped == pid_obsluga)
                nazwa = "obsługa";
            else if (reaped == pid_kucharz)
                nazwa = "kucharz";
            else if (reaped == pid_kierownik)
                nazwa = "kierownik";
            else if (reaped == pid_generator)
                nazwa = "generator";
            printf("Proces %d (%s) zakończył się (%d/4)\n", reaped, nazwa, licznik_procesow);
        }
        else if (reaped == 0)
        {
            // Brak procesów do zebrania - czekaj trochę
            usleep(50000); // 50ms
        }
        else if (reaped == -1 && errno == ECHILD)
        {
            // Nie ma już procesów potomnych
            printf("Wszystkie procesy zakończone.\n");
            break;
        }

        if (time(NULL) - czas_start > 3) // timeout 3 sekundy
        {
            printf("Timeout! Wymuszanie zakończenia pozostałych procesów...\n");

            // Najpierw SIGTERM - daj procesom możliwość uporządkowanego zakończenia
            if (pid_obsluga > 0)
                kill(pid_obsluga, SIGTERM);
            if (pid_kucharz > 0)
                kill(pid_kucharz, SIGTERM);
            if (pid_kierownik > 0)
                kill(pid_kierownik, SIGTERM);
            if (pid_generator > 0)
                kill(pid_generator, SIGTERM);

            usleep(500000); // 0.5 sekundy

            // Teraz SIGKILL dla upornych
            if (pid_obsluga > 0)
                kill(pid_obsluga, SIGKILL);
            if (pid_kucharz > 0)
                kill(pid_kucharz, SIGKILL);
            if (pid_kierownik > 0)
                kill(pid_kierownik, SIGKILL);
            if (pid_generator > 0)
                kill(pid_generator, SIGKILL);

            // Zabij wszystkie pozostałe procesy potomne poprzez usunięcie zasobów IPC
            // Procesy klientów mają alarm(30) więc zakończą się automatycznie

            break;
        }
        usleep(100000); // 100ms
    }

    sleep(1);

    // Wymuś usunięcie zasobów IPC
    shmctl(shm_id, IPC_RMID, NULL);
    semctl(sem_id, 0, IPC_RMID);

    // Poczekaj na procesy zombie
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;

    signal(SIGCHLD, SIG_IGN);

    printf("Program zakończony.\n");
    return 0;
}