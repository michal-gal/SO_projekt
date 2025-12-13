#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

// Stałe
#define MAX_QUEUE 100
#define MAX_BELT 50
#define MAX_TABLES 20
#define MAX_VIP 0.02 // 2%

// Struktury
typedef struct {
    int price; // 10,15,20 for basic, 40,50,60 for special
    int for_table; // -1 if general, else table id
} Plate;

typedef struct {
    int size; // 1-4
    int is_vip;
    int has_children; // 1 if group has children <=10
    int adult_supervisors; // adults supervising children
} Group;

typedef struct {
    int capacity; // 1,2,3,4
    int occupied; // current group size
    Group group;
} Table;

typedef struct {
    Plate belt[MAX_BELT];
    int head, tail, count;
} ConveyorBelt;

typedef struct {
    Group queue[MAX_QUEUE];
    int front, rear, count;
} CustomerQueue;

// Globalne zmienne
CustomerQueue customer_queue;
ConveyorBelt belt;
Table tables[MAX_TABLES];
int total_customers = 0;
int vip_count = 0;
int X1, X2, X3, X4; // Liczby stolików
int N; // Max osób
int P; // Max talerzyków
int Tp, Tk; // Godziny otwarcia
int signal = 0; // Sygnał kierownika
FILE *report;

// Funkcje pomocnicze
void init_belt() {
    belt.head = 0;
    belt.tail = 0;
    belt.count = 0;
}

void add_to_belt(Plate p) {
    if (belt.count < P) {
        belt.belt[belt.tail] = p;
        belt.tail = (belt.tail + 1) % MAX_BELT;
        belt.count++;
    }
}

Plate remove_from_belt() {
    Plate p = {0, -1};
    if (belt.count > 0) {
        p = belt.belt[belt.head];
        belt.head = (belt.head + 1) % MAX_BELT;
        belt.count--;
    }
    return p;
}

void init_queue() {
    customer_queue.front = 0;
    customer_queue.rear = 0;
    customer_queue.count = 0;
}

void enqueue(Group g) {
    if (customer_queue.count < MAX_QUEUE) {
        customer_queue.queue[customer_queue.rear] = g;
        customer_queue.rear = (customer_queue.rear + 1) % MAX_QUEUE;
        customer_queue.count++;
    }
}

Group dequeue() {
    Group g = {0,0,0,0};
    if (customer_queue.count > 0) {
        g = customer_queue.queue[customer_queue.front];
        customer_queue.front = (customer_queue.front + 1) % MAX_QUEUE;
        customer_queue.count--;
    }
    return g;
}

// Procedura Klient
void *client_thread(void *arg) {
    // Symulacja przybycia klientów
    while (1) {
        sleep(rand() % 10); // Losowy czas przybycia
        Group g;
        g.size = rand() % 4 + 1; // 1-4 osoby
        g.is_vip = (rand() % 100 < 2) ? 1 : 0; // 2% VIP
        g.has_children = rand() % 2; // Losowo dzieci
        g.adult_supervisors = g.has_children ? rand() % 3 + 1 : 0; // 1-3 opiekunów
        
        if (g.is_vip) {
            vip_count++;
            // VIP bezpośrednio do stolika
            fprintf(report, "VIP grupa %d osób przybyła.\n", g.size);
            // Znajdź wolny stolik
            for (int i = 0; i < MAX_TABLES; i++) {
                if (tables[i].occupied == 0 && tables[i].capacity >= g.size) {
                    tables[i].occupied = g.size;
                    tables[i].group = g;
                    break;
                }
            }
        } else {
            enqueue(g);
            fprintf(report, "Grupa %d osób w kolejce.\n", g.size);
        }
    }
    return NULL;
}

// Procedura Obsługa
void *staff_thread(void *arg) {
    while (1) {
        // Obsługa kolejki
        if (customer_queue.count > 0) {
            Group g = dequeue();
            // Przydziel miejsce
            int assigned = 0;
            for (int i = 0; i < MAX_TABLES; i++) {
                if (tables[i].occupied == 0 && tables[i].capacity >= g.size) {
                    tables[i].occupied = g.size;
                    tables[i].group = g;
                    assigned = 1;
                    fprintf(report, "Grupa %d osób przy stoliku %d.\n", g.size, i);
                    break;
                }
            }
            if (!assigned) {
                fprintf(report, "Brak miejsca dla grupy %d osób.\n", g.size);
            }
        }
        
        // Serwowanie dań podstawowych
        int speed = 1;
        if (signal == 1) speed = 2;
        else if (signal == 2) speed = 0.5;
        
        for (int i = 0; i < speed; i++) {
            Plate p;
            p.price = (rand() % 3 + 1) * 10; // 10,15,20
            p.for_table = -1;
            add_to_belt(p);
            fprintf(report, "Dodano talerzyk %d zł na taśmę.\n", p.price);
        }
        
        sleep(1);
    }
    return NULL;
}

// Procedura Kucharz
void *chef_thread(void *arg) {
    // Kucharz obsługuje zamówienia specjalne, ale uproszczone
    while (1) {
        // Symulacja przygotowania specjalnych dań
        sleep(5);
        Plate p;
        p.price = (rand() % 3 + 4) * 10; // 40,50,60
        p.for_table = rand() % MAX_TABLES; // Dla losowego stolika
        add_to_belt(p);
        fprintf(report, "Specjalne danie %d zł dla stolika %d.\n", p.price, p.for_table);
    }
    return NULL;
}

// Procedura Kierownik
void *manager_thread(void *arg) {
    while (1) {
        sleep(20); // Co jakiś czas
        signal = rand() % 4; // 0-3
        if (signal == 3) {
            // Wszyscy wychodzą
            for (int i = 0; i < MAX_TABLES; i++) {
                tables[i].occupied = 0;
            }
            customer_queue.count = 0;
            fprintf(report, "Sygnał 3: Wszyscy opuszczają restaurację.\n");
        } else {
            fprintf(report, "Sygnał kierownika: %d\n", signal);
        }
    }
    return NULL;
}

int main() {
    srand(time(NULL));
    report = fopen("raport.txt", "w");
    
    // Inicjalizacja
    init_belt();
    init_queue();
    // Załóżmy przykładowe wartości
    X1 = 5; X2 = 3; X3 = 2; X4 = 1;
    N = X1 + 2*X2 + 3*X3 + 4*X4;
    P = 50;
    Tp = 10; Tk = 22; // Godziny
    
    // Inicjalizuj stoliki
    int idx = 0;
    for (int i = 0; i < X1; i++) tables[idx++].capacity = 1;
    for (int i = 0; i < X2; i++) tables[idx++].capacity = 2;
    for (int i = 0; i < X3; i++) tables[idx++].capacity = 3;
    for (int i = 0; i < X4; i++) tables[idx++].capacity = 4;
    
    // Wątki
    pthread_t client_t, staff_t, chef_t, manager_t;
    pthread_create(&client_t, NULL, client_thread, NULL);
    pthread_create(&staff_t, NULL, staff_thread, NULL);
    pthread_create(&chef_t, NULL, chef_thread, NULL);
    pthread_create(&manager_t, NULL, manager_thread, NULL);
    
    // Symulacja przez 60 sekund
    sleep(60);
    
    // Podsumowanie
    fprintf(report, "Podsumowanie: Taśma ma %d talerzyków.\n", belt.count);
    // Dodaj więcej podsumowań
    
    fclose(report);
    return 0;
}