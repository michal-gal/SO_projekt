CC = gcc
CFLAGS = -Wall -g -pthread
TARGET = restauracja
SOURCES = restauracja.c procesy.c
HEADERS = procesy.h
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS)

restauracja.o: restauracja.c $(HEADERS)
	$(CC) $(CFLAGS) -c restauracja.c

procesy.o: procesy.c $(HEADERS)
	$(CC) $(CFLAGS) -c procesy.c

 

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean