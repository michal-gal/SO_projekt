#include "restauracja.h"

// ====== ZMIENNE GLOBALNE ======
int shm_id, sem_id;
Kolejka *kolejka;
Stolik *stoliki;
Tasma *tasma;
Statystyki *kuchnia, *kasa;
int *sygnal_kierownika;
int *restauracja_otwarta;

// ====== MAIN ======
int main()
{

    srand(time(NULL));
    int bufor = sizeof(Kolejka) +              //
                sizeof(Stolik) * MAX_STOLIKI + //
                sizeof(Tasma) +                //
                sizeof(Statystyki) * 2 +       //
                sizeof(int) * 2;
    shm_id = shmget(IPC_PRIVATE, bufor, IPC_CREAT | 0666); // utworzenie pamięci współdzielonej
    void *pamiec_wspoldzielona = shmat(shm_id, NULL, 0);   // dołączenie pamięci współdzielonej

    kolejka = pamiec_wspoldzielona;
    stoliki = (void *)(kolejka + 1);
    tasma = (void *)(stoliki + MAX_STOLIKI);
    kuchnia = (void *)(tasma + 1);
    kasa = kuchnia + 1;
    sygnal_kierownika = (void *)(kasa + 1);
    restauracja_otwarta = sygnal_kierownika + 1;

    sem_id = semget(IPC_PRIVATE, 3, IPC_CREAT | 0666); // utworzenie zestawu semaforów
    semctl(sem_id, SEM_KOLEJKA, SETVAL, 1);            // inicjalizacja semaforów
    semctl(sem_id, SEM_STOLIKI, SETVAL, 1);
    semctl(sem_id, SEM_TASMA, SETVAL, 1);

    for (int i = 0; i < MAX_STOLIKI; i++)
    {
        stoliki[i].pojemnosc = (i % 4) + 1;
        stoliki[i].zajety = 0;
    }

    *restauracja_otwarta = 1;

    pid_t pid = fork();
    if (pid == 0)
    {
        klient();
    }
    else if (pid == -1)
    {
        perror("fork failed");
        exit(1);
    }

    pid = fork();
    if (pid == 0)
    {
        klient();
    }
    else if (pid == -1)
    {
        perror("fork failed");
        exit(1);
    }

    pid = fork();
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

    sleep(CZAS_PRACY);
    *restauracja_otwarta = 0;

    sleep(3);
    shmctl(shm_id, IPC_RMID, NULL);
    semctl(sem_id, 0, IPC_RMID);

    return 0;
}