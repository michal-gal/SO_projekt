#include "procesy.h"

// ====== ZMIENNE GLOBALNE (DEFINICJE) ======
int shm_id, sem_id;
struct Kolejka *kolejka;
struct Stolik *stoliki;
int *sygnal_kierownika;
int *restauracja_otwarta;
int *aktywni_klienci;
int *kuchnia_dania_wydane;
int *kasa_dania_sprzedane;
struct Talerzyk *tasma;
int *kolej_podsumowania;
pid_t pid_obsluga, pid_kucharz, pid_kierownik, pid_generator;

const int ILOSC_STOLIKOW[4] = {X1, X2, X3, X4};
const int CENY_DAN[6] = {p10, p15, p20, p40, p50, p60};
