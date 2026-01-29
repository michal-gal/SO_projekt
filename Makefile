CC = gcc

# Build-time configurable values (override on make command line):
#   make CLIENTS_TO_CREATE=500 ...
# Backwards-compatible: `LICZBA_GRUP` mirrors `CLIENTS_TO_CREATE` unless set explicitly.
LOG_LEVEL ?= 1
CLIENTS_TO_CREATE ?= 1000
LICZBA_GRUP ?= $(CLIENTS_TO_CREATE)
CZAS_PRACY_SEKUNDY ?= 10

# Compiler/linker flags
CFLAGS = -Wall -g
CFLAGS += -DLOG_LEVEL=$(LOG_LEVEL)
CFLAGS += # MAX_LOSOWYCH_GRUP and CLIENTS_TO_CREATE are controlled at runtime via program arguments
CFLAGS += -DCZAS_PRACY=$(CZAS_PRACY_SEKUNDY)
CFLAGS += $(EXTRA_CFLAGS)
LDFLAGS = -pthread

TARGET = restauracja
PROCS = klient obsluga kucharz kierownik
HEADERS = common.h restauracja.h log.h

COMMON_OBJS = common.o log.o

OBJECTS_RESTAURACJA = restauracja.o $(COMMON_OBJS)
OBJECTS_KLIENT = klient.o $(COMMON_OBJS)
OBJECTS_OBSLUGA = obsluga.o $(COMMON_OBJS)
OBJECTS_KUCHARZ = kucharz.o $(COMMON_OBJS)
OBJECTS_KIEROWNIK = kierownik.o $(COMMON_OBJS)

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

log.o: log.c log.h
	$(CC) $(CFLAGS) -c log.c

klient.o: klient.c $(HEADERS)
	$(CC) $(CFLAGS) -c klient.c

obsluga.o: obsluga.c $(HEADERS)
	$(CC) $(CFLAGS) -c obsluga.c

kucharz.o: kucharz.c $(HEADERS)
	$(CC) $(CFLAGS) -c kucharz.c

kierownik.o: kierownik.c $(HEADERS)
	$(CC) $(CFLAGS) -c kierownik.c


clean:
	rm -f *.o $(TARGET) $(PROCS) generator

test: all
	./tests/test_smoke.sh
	./tests/test_signals.sh
	./tests/test_jobcontrol.sh
	./tests/test_no_orphans.sh

.PHONY: all clean test

help:
	@echo "Usage: make [VAR=value]"
	@echo "Variables you can override:"
	@echo "  CLIENTS_TO_CREATE   - (removed) use program args instead: ./restauracja <clients> <time> <log>"
	@echo "  LICZBA_GRUP         - (removed) alias; use runtime client count as first arg"
	@echo "  CZAS_PRACY_SEKUNDY  - default working time passed to program at compile-time (default=$(CZAS_PRACY_SEKUNDY))"
	@echo "Examples:"
	@echo "  make CLIENTS_TO_CREATE=500"
	@echo "  make LOG_LEVEL=2"
