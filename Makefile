CC = gcc
CFLAGS = -Wall -g -pthread
TARGET = restauracja
SOURCES = restauracja.c procesy.c globals.c
HEADERS = procesy.h
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS)

restauracja.o: restauracja.c $(HEADERS)
	$(CC) $(CFLAGS) -c restauracja.c

procesy.o: procesy.c $(HEADERS)
	$(CC) $(CFLAGS) -c procesy.c

globals.o: globals.c $(HEADERS)
	$(CC) $(CFLAGS) -c globals.c

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean