#include "restauracja.h"

// ====== ZMIENNE GLOBALNE ======
int shm_id, sem_id;        // ID pamięci współdzielonej i semaforów
struct Kolejka *kolejka;   // wskaźnik na kolejkę
struct Stolik *stoliki;    // wskaźnik na tablicę stolików
struct Tasma *tasma;       // wskaźnik na taśmę
int *sygnal_kierownika;    // wskaźnik na sygnał kierownika
int *restauracja_otwarta;  // wskaźnik na stan restauracji
int *kuchnia_dania_wydane; // liczba wydanych dań przez kuchnię
int *kasa_dania_sprzedane; // liczba sprzedanych dań przez kasę
// static const int ILOSC_STOLIKOW[4] = {X1, X2, X3, X4};         // liczba stolików o pojemności 1,2,3,4
// static const int CENY_DAN[6] = {p10, p15, p20, p40, p50, p60}; // ceny dań;

// ====== MAIN ======
int main()
{

    srand(time(NULL));
    int bufor = sizeof(struct Kolejka) +              // kolejka
                sizeof(struct Stolik) * MAX_STOLIKI + // stoliki
                sizeof(struct Tasma) +                // taśma
                sizeof(int) * 6 * 2 +                 // kuchnia i kasa - liczba dań
                sizeof(int) * 2 +                     // sygnał kierownika i stan restauracji
                sizeof(int) * 6;                      // pobrane dania

    shm_id = shmget(IPC_PRIVATE, bufor, IPC_CREAT | 0666); // utworzenie pamięci współdzielonej
    void *pamiec_wspoldzielona = shmat(shm_id, NULL, 0);   // dołączenie pamięci współdzielonej
    memset(pamiec_wspoldzielona, 0, bufor);                // wyzerowanie pamięci

    kolejka = pamiec_wspoldzielona;
    stoliki = (void *)(kolejka + 1);
    tasma = (void *)(stoliki + MAX_STOLIKI);
    kuchnia_dania_wydane = (int *)(tasma + 1);
    kasa_dania_sprzedane = kuchnia_dania_wydane + 6;
    sygnal_kierownika = kasa_dania_sprzedane + 6;
    restauracja_otwarta = sygnal_kierownika + 1;

    sem_id = semget(IPC_PRIVATE, 3, IPC_CREAT | 0666); // utworzenie zestawu semaforów
    semctl(sem_id, SEM_KOLEJKA, SETVAL, 1);            // inicjalizacja semaforów
    semctl(sem_id, SEM_STOLIKI, SETVAL, 1);
    semctl(sem_id, SEM_TASMA, SETVAL, 1);

    generator_stolikow(stoliki);

    *restauracja_otwarta = 1;

    pid_t pid = fork();
    if (pid == 0)
    {
        obsluga();
    }
    else if (pid == -1)
    {
        perror("fork failed");
        exit(1);
    }

    pid = fork();
    if (pid == 0)
    {
        kucharz();
    }
    else if (pid == -1)
    {
        perror("fork failed");
        exit(1);
    }

    pid = fork();
    if (pid == 0)
    {
        kierownik();
    }
    else if (pid == -1)
    {
        perror("fork failed");
        exit(1);
    }

    pid = fork();
    if (pid == 0)
    {
        generator_klientow();
    }
    else if (pid == -1)
    {
        perror("fork failed");
        exit(1);
    }

    sleep(CZAS_PRACY);
    *restauracja_otwarta = 0;

    sleep(3);
    shmctl(shm_id, IPC_RMID, NULL);
    semctl(sem_id, 0, IPC_RMID);
    signal(SIGCHLD, SIG_IGN); // zapobieganie procesom zombie

    return 0;
}