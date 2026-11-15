# Multi-threaded inverted file index - build rules
#
#   make          build ./search
#   make test     build, then run the automated tests
#   make bench    build, then run the benchmark (100 files x ~1MB by default)
#   make asan     rebuild with AddressSanitizer + UndefinedBehaviorSanitizer
#   make tsan     rebuild with ThreadSanitizer
#   make clean    remove the binary, object files and index files

CC       = gcc
CFLAGS   = -std=c99 -Wall -Wextra -O2 -pthread $(EXTRA_CFLAGS)
LDFLAGS  = -pthread $(EXTRA_CFLAGS)
TARGET   = search
SOURCES  = main.c hash_table.c index.c search.c
OBJECTS  = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

main.o:       main.c hash_table.h index.h search.h
hash_table.o: hash_table.c hash_table.h
index.o:      index.c index.h hash_table.h
search.o:     search.c search.h index.h hash_table.h

test: $(TARGET)
	sh tests/run_tests.sh

bench: $(TARGET)
	sh tests/benchmark.sh

asan: clean
	$(MAKE) EXTRA_CFLAGS="-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined"

tsan: clean
	$(MAKE) EXTRA_CFLAGS="-g -O1 -fsanitize=thread"

clean:
	rm -f $(TARGET) $(OBJECTS) index.dat index.dat.tmp

.PHONY: all test bench asan tsan clean
