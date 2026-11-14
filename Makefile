CC = gcc
CPPFLAGS = -Iutils
CFLAGS = -std=gnu11 -Wall -Wextra -Wpedantic -Werror -g

.PHONY: all test clean
all:
	$(MAKE) -C src

tests/%: tests/%.c src/libosmem.so
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< -Lsrc -losmem -Wl,-rpath,'$$ORIGIN/../src'

test: all tests/test_allocator tests/test_reference_layout
	./tests/test_allocator
	./tests/test_reference_layout

clean:
	$(MAKE) -C src clean
	rm -f tests/test_allocator tests/test_reference_layout
