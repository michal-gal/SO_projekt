#include "procesy.h"
#include <errno.h>

// ====== MAIN ======
int main()
{
    srand(time(NULL));
    stworz_ipc();
    generator_stolikow(stoliki);
    fflush(stdout); // opróżnij bufor przed fork() aby uniknąć duplikatów

    *restauracja_otwarta = 1;

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

    // Czekaj maksymalnie CZAS_PRACY lub do sygnału zamknięcia (np. sygnał 3 od kierownika)
    time_t start_czekania = time(NULL);
    while (time(NULL) - start_czekania < CZAS_PRACY && *restauracja_otwarta)
    {
        sleep(1);
    }
    *restauracja_otwarta = 0;
    printf("\n===Czas pracy restauracji minął!===\n");
    fflush(stdout); // upewnij się że komunikaty zostały wydrukowane

    // Daj chwilę na naturalne zakończenie klientów zanim zamkniemy procesy główne
    time_t koniec_klienci_start = time(NULL);
    while (*aktywni_klienci > 0 && time(NULL) - koniec_klienci_start < 25) // czekaj maksymalnie 25 sekund
    {
        printf("\r/ Czekam na klientów: %d\r", *aktywni_klienci);
        fflush(stdout);
        usleep(200000); // 200 ms
        printf("- Czekam na klientów: %d\r", *aktywni_klienci);
        fflush(stdout);
        usleep(200000); // 200 ms
        printf("\\ Czekam na klientów: %d\r", *aktywni_klienci);
        fflush(stdout);
        usleep(200000); // 200 ms
        printf("| Czekam na klientów: %d\r", *aktywni_klienci);
        fflush(stdout);
        usleep(200000); // 200 ms
        printf("/ Czekam na klientów: %d\r", *aktywni_klienci);
        fflush(stdout);
        usleep(200000); // 200 ms
        printf("- Czekam na klientów: %d\r", *aktywni_klienci);
        fflush(stdout);
        usleep(200000); // 200 ms
        printf("\\ Czekam na klientów: %d\r", *aktywni_klienci);
        fflush(stdout);
        usleep(200000); // 200 ms
        printf("| Czekam na klientów: %d", *aktywni_klienci);
        fflush(stdout);
        usleep(200000); // 200 ms
    }
    printf("\n");
    // Centralna sanity-check: jeśli kolejka pusta i wszystkie stoliki wolne,
    // wyzeruj licznik aktywnych klientów, aby procesy mogły się zakończyć.

    int kolejka_pusta;
    int stoliki_zajete = 0;

    sem_op(SEM_KOLEJKA, -1);
    kolejka_pusta = (kolejka->ilosc == 0);
    sem_op(SEM_KOLEJKA, 1);

    sem_op(SEM_STOLIKI, -1);
    for (int i = 0; i < MAX_STOLIKI; i++)
    {
        if (stoliki[i].proces_id != 0)
            stoliki_zajete++;
    }
    sem_op(SEM_STOLIKI, 1);

    if (kolejka_pusta && stoliki_zajete == 0)
    {
        sem_op(SEM_KOLEJKA, -1);
        *aktywni_klienci = 0;
        sem_op(SEM_KOLEJKA, 1);
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
    printf("Program zakończony.\n");
    return 0;
}