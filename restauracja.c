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
    shm_id = shmget(IPC_PRIVATE, sizeof(Kolejka) + sizeof(Stolik) * MAX_STOLIKI + sizeof(Tasma) + sizeof(Statystyki) * 2 + sizeof(int) * 2, IPC_CREAT | 0666);
    void *mem = shmat(shm_id, NULL, 0);

    kolejka = mem;
    stoliki = (void *)(kolejka + 1);
    tasma = (void *)(stoliki + MAX_STOLIKI);
    kuchnia = (void *)(tasma + 1);
    kasa = kuchnia + 1;
    sygnal_kierownika = (void *)(kasa + 1);
    restauracja_otwarta = sygnal_kierownika + 1;

    sem_id = semget(IPC_PRIVATE, 3, IPC_CREAT | 0666);
    semctl(sem_id, SEM_KOLEJKA, SETVAL, 1);
    semctl(sem_id, SEM_STOLIKI, SETVAL, 1);
    semctl(sem_id, SEM_TASMA, SETVAL, 1);

    for (int i = 0; i < MAX_STOLIKI; i++)
    {
        stoliki[i].pojemnosc = (i % 4) + 1;
        stoliki[i].zajety = 0;
    }

    *restauracja_otwarta = 1;

    if (!fork())
        klient();
    if (!fork())
        obsluga();
    if (!fork())
        kucharz();
    if (!fork())
        kierownik();

    sleep(CZAS_PRACY);
    *restauracja_otwarta = 0;

    sleep(3);
    shmctl(shm_id, IPC_RMID, NULL);
    semctl(sem_id, 0, IPC_RMID);

    return 0;
}