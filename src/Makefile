CC = gcc
CPPFLAGS = -I../utils
CFLAGS = -std=gnu11 -fPIC -Wall -Wextra -Wpedantic -Werror -g
LDFLAGS = -shared

TARGET = libosmem.so

.PHONY: all clean
all: $(TARGET)

$(TARGET): osmem.o
	$(CC) $(LDFLAGS) -o $@ $^

osmem.o: osmem.c ../utils/osmem.h ../utils/block_meta.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f osmem.o $(TARGET)
