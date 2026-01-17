CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -pthread
TARGET = restauracja
PROCS = klient obsluga kucharz kierownik
HEADERS = procesy.h restauracja.h

COMMON_OBJS = common.o

OBJECTS_RESTAURACJA = restauracja.o $(COMMON_OBJS)
OBJECTS_KLIENT = klient_main.o klient.o $(COMMON_OBJS)
OBJECTS_OBSLUGA = obsluga_main.o obsluga.o $(COMMON_OBJS)
OBJECTS_KUCHARZ = kucharz_main.o kucharz.o $(COMMON_OBJS)
OBJECTS_KIEROWNIK = kierownik_main.o kierownik.o $(COMMON_OBJS)

all: $(TARGET) $(PROCS)

$(TARGET): $(OBJECTS_RESTAURACJA)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(OBJECTS_RESTAURACJA)

klient: $(OBJECTS_KLIENT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS_KLIENT)

obsluga: $(OBJECTS_OBSLUGA)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS_OBSLUGA)

kucharz: $(OBJECTS_KUCHARZ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS_KUCHARZ)

kierownik: $(OBJECTS_KIEROWNIK)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS_KIEROWNIK)


restauracja.o: restauracja.c $(HEADERS)
	$(CC) $(CFLAGS) -c restauracja.c

common.o: common.c $(HEADERS)
	$(CC) $(CFLAGS) -c common.c

klient.o: klient.c $(HEADERS)
	$(CC) $(CFLAGS) -c klient.c

obsluga.o: obsluga.c $(HEADERS)
	$(CC) $(CFLAGS) -c obsluga.c

kucharz.o: kucharz.c $(HEADERS)
	$(CC) $(CFLAGS) -c kucharz.c

kierownik.o: kierownik.c $(HEADERS)
	$(CC) $(CFLAGS) -c kierownik.c


klient_main.o: klient_main.c $(HEADERS)
	$(CC) $(CFLAGS) -c klient_main.c

obsluga_main.o: obsluga_main.c $(HEADERS)
	$(CC) $(CFLAGS) -c obsluga_main.c

kucharz_main.o: kucharz_main.c $(HEADERS)
	$(CC) $(CFLAGS) -c kucharz_main.c

kierownik_main.o: kierownik_main.c $(HEADERS)
	$(CC) $(CFLAGS) -c kierownik_main.c


clean:
	rm -f *.o $(TARGET) $(PROCS) generator

.PHONY: all clean