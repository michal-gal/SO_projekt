#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <signal.h>
#include <fcntl.h>

// Definicje stałych
#define MAX_QUEUE 50
#define MAX_BELT 100
#define MAX_TABLES 10

// Struktury danych
typedef struct Grupa
{
    int size;
    int is_vip;
    int has_children;
    int adult_supervisors;
} Grupa;

typedef struct Talerz
{
    int price;
    int for_table;
} Talerz;

typedef struct Stolik
{
    int occupied;
    int capacity;
    Grupa grupa;
} Stolik;

typedef struct
{
    Grupa queue[MAX_QUEUE];
    int front;
    int rear;
    int count;
} Kolejka;

typedef struct
{
    Talerz belt[MAX_BELT];
    int head;
    int tail;
    int count;
} Tasma;

// Globalne zmienne - teraz współdzielone
Kolejka *customer_queue;
Tasma *belt;
Stolik *tables;
int *total_customers;
int *vip_count;
int *manager_signal;           // Sygnał kierownika
sem_t *queue_sem;              // Semafor dla kolejki
sem_t *belt_sem;               // Semafor dla taśmy
sem_t *table_sem;              // Semafor dla stolików
int P;                         // Maksymalna pojemność taśmy
int X1, X2, X3, X4, N, Tp, Tk; // Zmienne konfiguracyjne
FILE *report;                  // Plik raportu

// Funkcje pomocnicze - dostosuj do wskaźników
void init_belt()
{
    belt->head = 0;
    belt->tail = 0;
    belt->count = 0;
}

void add_to_belt(Talerz p)
{
    sem_wait(belt_sem);
    if (belt->count < P)
    {
        belt->belt[belt->tail] = p;
        belt->tail = (belt->tail + 1) % MAX_BELT;
        belt->count++;
    }
    sem_post(belt_sem);
}

Talerz remove_from_belt()
{
    Talerz p = {0, -1};
    sem_wait(belt_sem);
    if (belt->count > 0)
    {
        p = belt->belt[belt->head];
        belt->head = (belt->head + 1) % MAX_BELT;
        belt->count--;
    }
    sem_post(belt_sem);
    return p;
}

void init_queue()
{
    customer_queue->front = 0;
    customer_queue->rear = 0;
    customer_queue->count = 0;
}

void enqueue(Grupa g)
{
    sem_wait(queue_sem);
    if (customer_queue->count < MAX_QUEUE)
    {
        customer_queue->queue[customer_queue->rear] = g;
        customer_queue->rear = (customer_queue->rear + 1) % MAX_QUEUE;
        customer_queue->count++;
    }
    sem_post(queue_sem);
}

Grupa dequeue()
{
    Grupa g = {0, 0, 0, 0};
    sem_wait(queue_sem);
    if (customer_queue->count > 0)
    {
        g = customer_queue->queue[customer_queue->front];
        customer_queue->front = (customer_queue->front + 1) % MAX_QUEUE;
        customer_queue->count--;
    }
    sem_post(queue_sem);
    return g;
}

void client_process()
{
    while (1)
    {
        sleep(rand() % 10);
        Grupa g;
        g.size = rand() % 4 + 1;
        g.is_vip = (rand() % 100 < 2) ? 1 : 0;
        g.has_children = rand() % 2;
        g.adult_supervisors = g.has_children ? rand() % 3 + 1 : 0;

        if (g.is_vip)
        {
            (*vip_count)++;
            fprintf(report, "VIP grupa %d osób przybyła.\n", g.size);
            sem_wait(table_sem);
            for (int i = 0; i < MAX_TABLES; i++)
            {
                if (tables[i].occupied == 0 && tables[i].capacity >= g.size)
                {
                    tables[i].occupied = g.size;
                    tables[i].grupa = g;
                    break;
                }
            }
            sem_post(table_sem);
        }
        else
        {
            enqueue(g);
            fprintf(report, "Grupa %d osób w kolejce.\n", g.size);
        }
    }
}

void staff_process()
{
    while (1)
    {
        if (customer_queue->count > 0)
        {
            Grupa g = dequeue();
            int assigned = 0;
            sem_wait(table_sem);
            for (int i = 0; i < MAX_TABLES; i++)
            {
                if (tables[i].occupied == 0 && tables[i].capacity >= g.size)
                {
                    tables[i].occupied = g.size;
                    tables[i].grupa = g;
                    assigned = 1;
                    fprintf(report, "Grupa %d osób przy stoliku %d.\n", g.size, i);
                    break;
                }
            }
            sem_post(table_sem);
            if (!assigned)
            {
                fprintf(report, "Brak miejsca dla grupy %d osób.\n", g.size);
            }
        }

        int speed = 1;
        if (*manager_signal == 1)
            speed = 2;
        else if (*manager_signal == 2)
            speed = 0.5;

        for (int i = 0; i < speed; i++)
        {
            Talerz p;
            p.price = (rand() % 3 + 1) * 10;
            p.for_table = -1;
            add_to_belt(p);
            fprintf(report, "Dodano talerzyk %d zł na taśmę.\n", p.price);
        }
        sleep(1);
    }
}

void chef_process()
{
    while (1)
    {
        sleep(5);
        Talerz p;
        p.price = (rand() % 3 + 4) * 10;
        p.for_table = rand() % MAX_TABLES;
        add_to_belt(p);
        fprintf(report, "Specjalne danie %d zł dla stolika %d.\n", p.price, p.for_table);
    }
}

void manager_process()
{
    while (1)
    {
        sleep(20);
        *manager_signal = rand() % 4;
        if (*manager_signal == 3)
        {
            sem_wait(table_sem);
            for (int i = 0; i < MAX_TABLES; i++)
            {
                tables[i].occupied = 0;
            }
            sem_wait(queue_sem);
            customer_queue->count = 0;
            sem_post(queue_sem);
            sem_post(table_sem);
            fprintf(report, "Sygnał 3: Wszyscy opuszczają restaurację.\n");
        }
        else
        {
            fprintf(report, "Sygnał kierownika: %d\n", *manager_signal);
        }
    }
}

int main()
{
    srand(time(NULL));
    report = fopen("raport.txt", "w");

    // Tworzenie współdzielonej pamięci
    int shm_queue = shmget(IPC_PRIVATE, sizeof(Kolejka), IPC_CREAT | 0666);
    int shm_belt = shmget(IPC_PRIVATE, sizeof(Tasma), IPC_CREAT | 0666);
    int shm_tables = shmget(IPC_PRIVATE, sizeof(Stolik) * MAX_TABLES, IPC_CREAT | 0666);
    int shm_total = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    int shm_vip = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    int shm_signal = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);

    customer_queue = (Kolejka *)shmat(shm_queue, NULL, 0);
    belt = (Tasma *)shmat(shm_belt, NULL, 0);
    tables = (Stolik *)shmat(shm_tables, NULL, 0);
    total_customers = (int *)shmat(shm_total, NULL, 0);
    vip_count = (int *)shmat(shm_vip, NULL, 0);
    manager_signal = (int *)shmat(shm_signal, NULL, 0);

    // Inicjalizacja semaforów
    queue_sem = sem_open("/queue_sem", O_CREAT, 0644, 1);
    belt_sem = sem_open("/belt_sem", O_CREAT, 0644, 1);
    table_sem = sem_open("/table_sem", O_CREAT, 0644, 1);

    // Inicjalizacja
    init_belt();
    init_queue();
    *total_customers = 0;
    *vip_count = 0;
    *manager_signal = 0;
    X1 = 5;
    X2 = 3;
    X3 = 2;
    X4 = 1;
    N = X1 + 2 * X2 + 3 * X3 + 4 * X4;
    P = 50;
    Tp = 10;
    Tk = 22;

    int idx = 0;
    for (int i = 0; i < X1; i++)
        tables[idx++].capacity = 1;
    for (int i = 0; i < X2; i++)
        tables[idx++].capacity = 2;
    for (int i = 0; i < X3; i++)
        tables[idx++].capacity = 3;
    for (int i = 0; i < X4; i++)
        tables[idx++].capacity = 4;

    // Tworzenie procesów
    pid_t pid_client = fork();
    if (pid_client == 0)
    {
        client_process();
        exit(0);
    }

    pid_t pid_staff = fork();
    if (pid_staff == 0)
    {
        staff_process();
        exit(0);
    }

    pid_t pid_chef = fork();
    if (pid_chef == 0)
    {
        chef_process();
        exit(0);
    }

    pid_t pid_manager = fork();
    if (pid_manager == 0)
    {
        manager_process();
        exit(0);
    }

    // Główny proces: symulacja przez 60 sekund
    sleep(20);

    // Zabij procesy potomne
    kill(pid_client, SIGTERM);
    kill(pid_staff, SIGTERM);
    kill(pid_chef, SIGTERM);
    kill(pid_manager, SIGTERM);

    // Podsumowanie
    fprintf(report, "Podsumowanie: Taśma ma %d talerzyków.\n", belt->count);

    // Czyszczenie
    shmdt(customer_queue);
    shmctl(shm_queue, IPC_RMID, NULL);
    shmdt(belt);
    shmctl(shm_belt, IPC_RMID, NULL);
    shmdt(tables);
    shmctl(shm_tables, IPC_RMID, NULL);
    shmdt(total_customers);
    shmctl(shm_total, IPC_RMID, NULL);
    shmdt(vip_count);
    shmctl(shm_vip, IPC_RMID, NULL);
    shmdt(signal);
    shmctl(shm_signal, IPC_RMID, NULL);
    sem_close(queue_sem);
    sem_unlink("/queue_sem");
    sem_close(belt_sem);
    sem_unlink("/belt_sem");
    sem_close(table_sem);
    sem_unlink("/table_sem");

    fclose(report);
    return 0;
}