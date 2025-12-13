#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h> // Usuń, jeśli nie używasz wątków
#include <unistd.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <semaphore.h>

// Stałe
#define MAX_QUEUE 100
#define MAX_BELT 50
#define MAX_TABLES 20
#define MAX_VIP 0.02 // 2%

// Struktury
typedef struct
{
    int price;     // 10,15,20 for basic, 40,50,60 for special
    int for_table; // -1 if general, else table id
} Plate;

typedef struct
{
    int size; // 1-4
    int is_vip;
    int has_children;      // 1 if group has children <=10
    int adult_supervisors; // adults supervising children
} Group;

typedef struct
{
    int capacity; // 1,2,3,4
    int occupied; // current group size
    Group group;
} Table;

typedef struct
{
    Plate belt[MAX_BELT];
    int head, tail, count;
} ConveyorBelt;

typedef struct
{
    Group queue[MAX_QUEUE];
    int front, rear, count;
} CustomerQueue;

// Globalne zmienne - teraz współdzielone
CustomerQueue *customer_queue;
ConveyorBelt *belt;
Table *tables;
int *total_customers;
int *vip_count;
int *signal;      // Sygnał kierownika
sem_t *queue_sem; // Semafor dla kolejki
sem_t *belt_sem;  // Semafor dla taśmy
sem_t *table_sem; // Semafor dla stolików

// Funkcje pomocnicze - dostosuj do wskaźników
void init_belt()
{
    belt->head = 0;
    belt->tail = 0;
    belt->count = 0;
}

void add_to_belt(Plate p)
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

Plate remove_from_belt()
{
    Plate p = {0, -1};
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

void enqueue(Group g)
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

Group dequeue()
{
    Group g = {0, 0, 0, 0};
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

// Procedury - bez pthread, jako funkcje dla procesów
void client_process()
{
    while (1)
    {
        sleep(rand() % 10);
        Group g;
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
                    tables[i].group = g;
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
            Group g = dequeue();
            int assigned = 0;
            sem_wait(table_sem);
            for (int i = 0; i < MAX_TABLES; i++)
            {
                if (tables[i].occupied == 0 && tables[i].capacity >= g.size)
                {
                    tables[i].occupied = g.size;
                    tables[i].group = g;
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
        if (*signal == 1)
            speed = 2;
        else if (*signal == 2)
            speed = 0.5;

        for (int i = 0; i < speed; i++)
        {
            Plate p;
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
        Plate p;
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
        *signal = rand() % 4;
        if (*signal == 3)
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
            fprintf(report, "Sygnał kierownika: %d\n", *signal);
        }
    }
}

int main()
{
    srand(time(NULL));
    report = fopen("raport.txt", "w");

    // Tworzenie współdzielonej pamięci
    int shm_queue = shmget(IPC_PRIVATE, sizeof(CustomerQueue), IPC_CREAT | 0666);
    int shm_belt = shmget(IPC_PRIVATE, sizeof(ConveyorBelt), IPC_CREAT | 0666);
    int shm_tables = shmget(IPC_PRIVATE, sizeof(Table) * MAX_TABLES, IPC_CREAT | 0666);
    int shm_total = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    int shm_vip = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    int shm_signal = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);

    customer_queue = (CustomerQueue *)shmat(shm_queue, NULL, 0);
    belt = (ConveyorBelt *)shmat(shm_belt, NULL, 0);
    tables = (Table *)shmat(shm_tables, NULL, 0);
    total_customers = (int *)shmat(shm_total, NULL, 0);
    vip_count = (int *)shmat(shm_vip, NULL, 0);
    signal = (int *)shmat(shm_signal, NULL, 0);

    // Inicjalizacja semaforów
    queue_sem = sem_open("/queue_sem", O_CREAT, 0644, 1);
    belt_sem = sem_open("/belt_sem", O_CREAT, 0644, 1);
    table_sem = sem_open("/table_sem", O_CREAT, 0644, 1);

    // Inicjalizacja
    init_belt();
    init_queue();
    *total_customers = 0;
    *vip_count = 0;
    *signal = 0;
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