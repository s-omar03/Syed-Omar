# TFTP client and server (RFC 1350) - build rules
#
#   make          build ./tftp_server and ./tftp_client
#   make server   build ./tftp_server only
#   make client   build ./tftp_client only
#   make test     build, then run the automated tests (tests/run_tests.sh)
#   make asan     rebuild everything with AddressSanitizer + UndefinedBehaviorSanitizer;
#                 run "make test" afterwards to test that build
#   make clean    remove the programs, test helpers and object files

CC        = gcc
CFLAGS    = -std=c99 -Wall -Wextra -O2 -D_FILE_OFFSET_BITS=64 $(EXTRA_CFLAGS)
LDFLAGS   = $(EXTRA_CFLAGS)

SERVER    = tftp_server
CLIENT    = tftp_client
COMMON    = tftp.o file.o netascii.o udp.o
TEST_BINS = tests/unit_tests tests/tftp_probe

all: $(SERVER) $(CLIENT)

server: $(SERVER)

client: $(CLIENT)

$(SERVER): server.o $(COMMON)
	$(CC) server.o $(COMMON) -o $@ $(LDFLAGS)

$(CLIENT): client.o $(COMMON)
	$(CC) client.o $(COMMON) -o $@ $(LDFLAGS)

tests/unit_tests: tests/unit_tests.o $(COMMON)
	$(CC) tests/unit_tests.o $(COMMON) -o $@ $(LDFLAGS)

tests/tftp_probe: tests/tftp_probe.o
	$(CC) tests/tftp_probe.o -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

server.o:            server.c tftp.h file.h netascii.h udp.h
client.o:            client.c tftp.h file.h netascii.h udp.h
tftp.o:              tftp.c tftp.h file.h netascii.h udp.h
file.o:              file.c file.h netascii.h
netascii.o:          netascii.c netascii.h
udp.o:               udp.c udp.h
tests/unit_tests.o:  tests/unit_tests.c tftp.h file.h netascii.h udp.h
tests/tftp_probe.o:  tests/tftp_probe.c

test-tools: $(TEST_BINS)

test: all test-tools
	bash tests/run_tests.sh

asan: clean
	$(MAKE) EXTRA_CFLAGS="-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined" all test-tools

clean:
	rm -f $(SERVER) $(CLIENT) $(TEST_BINS) *.o tests/*.o

.PHONY: all server client test test-tools asan clean
