CC = gcc

# Build-time configurable values (override on make command line):
# Historically this Makefile allowed overriding defaults like client count
# at compile time. Defaults are now read at runtime from environment
# variables (RESTAURACJA_LICZBA_KLIENTOW, RESTAURACJA_LOG_LEVEL,
# RESTAURACJA_CZAS_PRACY). Leave compiler flags minimal.

# Compiler/linker flags
CFLAGS = -Wall -g -Iinclude
CFLAGS += # CLIENTS are controlled at runtime via program arguments / env
# Do not pass CLIENTS_TO_CREATE as a compile-time macro anymore.
# The program reads default client count from environment variable
# RESTAURACJA_LICZBA_KLIENTOW at runtime.
CFLAGS += # LOG_LEVEL and CZAS_PRACY are read from environment at runtime
CFLAGS += $(EXTRA_CFLAGS)
LDFLAGS = -pthread

BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj

TARGET = restauracja
PROCS = klient obsluga szatnia kucharz kierownik
HEADERS = include/common.h include/restauracja.h include/log.h include/obsluga.h include/kucharz.h include/kierownik.h include/klient.h include/szatnia.h

COMMON_OBJS = $(OBJ_DIR)/common.o $(OBJ_DIR)/log.o

OBJECTS_RESTAURACJA = $(OBJ_DIR)/restauracja.o $(COMMON_OBJS)
OBJECTS_KLIENT = $(OBJ_DIR)/klient.o $(COMMON_OBJS)
OBJECTS_OBSLUGA = $(OBJ_DIR)/obsluga.o $(COMMON_OBJS)
OBJECTS_SZATNIA = $(OBJ_DIR)/szatnia.o $(COMMON_OBJS)
OBJECTS_KUCHARZ = $(OBJ_DIR)/kucharz.o $(COMMON_OBJS)
OBJECTS_KIEROWNIK = $(OBJ_DIR)/kierownik.o $(COMMON_OBJS)

all: $(TARGET) $(PROCS)

$(TARGET): $(OBJECTS_RESTAURACJA)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(OBJECTS_RESTAURACJA)

klient: $(OBJECTS_KLIENT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS_KLIENT)

obsluga: $(OBJECTS_OBSLUGA)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS_OBSLUGA)

szatnia: $(OBJECTS_SZATNIA)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS_SZATNIA)

kucharz: $(OBJECTS_KUCHARZ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS_KUCHARZ)

kierownik: $(OBJECTS_KIEROWNIK)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS_KIEROWNIK)


$(OBJ_DIR)/restauracja.o: src/restauracja.c $(HEADERS)
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c src/restauracja.c -o $(OBJ_DIR)/restauracja.o

$(OBJ_DIR)/common.o: src/common.c $(HEADERS)
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c src/common.c -o $(OBJ_DIR)/common.o

$(OBJ_DIR)/log.o: src/log.c include/log.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c src/log.c -o $(OBJ_DIR)/log.o


$(OBJ_DIR)/klient.o: src/klient.c $(HEADERS)
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c src/klient.c -o $(OBJ_DIR)/klient.o

$(OBJ_DIR)/obsluga.o: src/obsluga.c $(HEADERS)
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c src/obsluga.c -o $(OBJ_DIR)/obsluga.o

$(OBJ_DIR)/szatnia.o: src/szatnia.c $(HEADERS)
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c src/szatnia.c -o $(OBJ_DIR)/szatnia.o

$(OBJ_DIR)/kucharz.o: src/kucharz.c $(HEADERS)
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c src/kucharz.c -o $(OBJ_DIR)/kucharz.o

$(OBJ_DIR)/kierownik.o: src/kierownik.c $(HEADERS)
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c src/kierownik.c -o $(OBJ_DIR)/kierownik.o


clean:
	rm -f $(TARGET) $(PROCS) generator
	rm -rf $(OBJ_DIR)

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
