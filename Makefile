CC = gcc

# Build-time configurable values (override on make command line):
# Historically this Makefile allowed overriding defaults like client count
# at compile time. Defaults are now read at runtime from environment
# variables (RESTAURACJA_LICZBA_KLIENTOW, RESTAURACJA_LOG_LEVEL,
# RESTAURACJA_CZAS_PRACY). Leave compiler flags minimal.

# Compiler/linker flags
CFLAGS = -Wall -g
CFLAGS += # CLIENTS are controlled at runtime via program arguments / env
# Do not pass CLIENTS_TO_CREATE as a compile-time macro anymore.
# The program reads default client count from environment variable
# RESTAURACJA_LICZBA_KLIENTOW at runtime.
CFLAGS += # LOG_LEVEL and CZAS_PRACY are read from environment at runtime
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
	@echo "  RESTAURACJA_LICZBA_KLIENTOW - default client count (env)"
	@echo "  RESTAURACJA_LOG_LEVEL       - log level (env)"
	@echo "  RESTAURACJA_CZAS_PRACY      - runtime working time (env)"
	@echo "Notes: the compile-time macro CZAS_PRACY (common.h) provides the"
	@echo "  compile-time default. The program uses the following precedence:"
	@echo "  1) program argument <czas_sekund> (2nd arg), 2) RESTAURACJA_CZAS_PRACY"
	@echo "  environment variable, 3) CZAS_PRACY / czas_pracy_domyslny (from common.h)."
	@echo "Examples:"
	@echo "  export RESTAURACJA_LICZBA_KLIENTOW=500; ./restauracja"
	@echo "  export RESTAURACJA_LOG_LEVEL=2; ./restauracja"
