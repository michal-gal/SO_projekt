RESTauracja — instrukcja uruchomienia
===============================

Krótko
-----
- Buduj projekt poleceniem: `make`.
- Program jest sterowany RUNTIME argumentami — liczba grup/klientów nie jest już ustawiana przy kompilacji.

Szybkie przykłady
------------------
- Zbuduj:

```
make
```

- Uruchom symulację z 20000 grup, 5 sekundami pracy i log level 1:

```
./restauracja 20000 5 1
```

Argumenty programu
-------------------
- `<liczba_klientow>` — ile grup klientów stworzyć (pierwszy argument).
- `<czas_sekund>` — czas pracy restauracji w sekundach (drugi argument).
- `<log_level>` — poziom logowania (0..2) (trzeci argument).

Domyślny czas pracy
-------------------
- Kompilacyjny default: `CZAS_PRACY` (z `common.h`) jest domyślną wartością
	czasu pracy używaną, gdy nie podasz runtime override.
- Kolejność nadpisywania (precedence): 1) drugi argument programu
	(`<czas_sekund>`), 2) zmienna środowiskowa `RESTAURACJA_CZAS_PRACY`,
	3) `CZAS_PRACY` / `czas_pracy_domyslny`.

Makefile / budowanie
--------------------
- Zamiast przekazywać liczbę klientów w czasie kompilacji, ustawiasz ją bezpośrednio przy uruchomieniu programu (pierwszy argument).
- Możesz zobaczyć pomoc `Makefile` poleceniem:

```
make help
```

Przykłady użycia/testów
-----------------------
- Uruchom wszystkie testy (skrypty):

```
make && ./tests/test_smoke.sh
```

Ustawienia środowiskowe przydatne podczas testów
-----------------------------------------------
- `LOG_LEVEL` — jeśli chcesz ustawić inny poziom logowania dla potomnych procesów (można też podać trzeci argument programu).
- `RESTAURACJA_MAX_AKTYWNYCH_KLIENTOW` — (runtime) limit aktywnych klientów; można ustawić przed uruchomieniem programu.

Krótkie uwagi
-------------
- Liczba klientów/grup jest teraz sterowana tylko przez argument wywołania programu — to ułatwia testowanie i debugowanie bez rekompilacji.
- Jeśli chcesz przywrócić kompilacyjne makra (rzadkie), można to zrobić edytując `Makefile`.

Pliki istotne
-------------
- `restauracja` — program nadrzędny (starter). Argumenty jak powyżej.
- `klient`, `obsluga`, `kucharz`, `kierownik` — procesy potomne uruchamiane przez `restauracja`.

Jeśli chcesz, mogę dodać przykład `docker`/CI albo dodatkowe opcje runtime (np. losowy seed przez env). 
