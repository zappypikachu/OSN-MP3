CC = gcc

CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -pthread

TARGET = airport-troubles

SOURCES = airport.c bag.c clock.c scheduler.c

OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean sanitize

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

sanitize:
	$(CC) -std=c11 -g -Wall -Wextra -Wpedantic \
	-fsanitize=address,undefined -fno-omit-frame-pointer \
	-pthread $(SOURCES) -o airport-troubles-sanitize

clean:
	rm -f $(OBJECTS) $(TARGET) airport-troubles-sanitize
