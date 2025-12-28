CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -pthread
TARGET = restauracja
SOURCES = restauracja.c procesy.c pomocnicze.c 
HEADERS = restauracja.h 
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(OBJECTS)

restauracja.o: restauracja.c $(HEADERS)
	$(CC) $(CFLAGS) -c restauracja.c

procesy.o: procesy.c $(HEADERS)
	$(CC) $(CFLAGS) -c procesy.c

pomocnicze.o: pomocnicze.c $(HEADERS)
	$(CC) $(CFLAGS) -c pomocnicze.c
clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean