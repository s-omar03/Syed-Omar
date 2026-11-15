# TCP/IP based remote management solution - build rules
#
#   make          build ./server and ./client
#   make test     build, then run the automated tests
#   make asan     rebuild with AddressSanitizer + UndefinedBehaviorSanitizer
#   make clean    remove the programs and object files

CC          = gcc
CFLAGS      = -std=c99 -Wall -Wextra -O2 $(EXTRA_CFLAGS)
LDFLAGS     = $(EXTRA_CFLAGS)

SERVER      = server
CLIENT      = client

COMMON_SRC  = protocol.c util.c
SERVER_SRC  = server.c session.c diag.c ports.c procinfo.c strbuf.c
CLIENT_SRC  = client.c lineedit.c history.c

COMMON_OBJ  = $(COMMON_SRC:.c=.o)
SERVER_OBJ  = $(SERVER_SRC:.c=.o) $(COMMON_OBJ)
CLIENT_OBJ  = $(CLIENT_SRC:.c=.o) $(COMMON_OBJ)

all: $(SERVER) $(CLIENT)

$(SERVER): $(SERVER_OBJ)
	$(CC) $(SERVER_OBJ) -o $@ $(LDFLAGS)

$(CLIENT): $(CLIENT_OBJ)
	$(CC) $(CLIENT_OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

protocol.o: protocol.c protocol.h util.h
util.o:     util.c util.h
server.o:   server.c protocol.h session.h util.h
session.o:  session.c session.h diag.h ports.h procinfo.h protocol.h strbuf.h util.h
diag.o:     diag.c diag.h procinfo.h protocol.h strbuf.h util.h
ports.o:    ports.c ports.h diag.h procinfo.h protocol.h strbuf.h
procinfo.o: procinfo.c procinfo.h
strbuf.o:   strbuf.c strbuf.h
client.o:   client.c history.h lineedit.h protocol.h util.h
lineedit.o: lineedit.c lineedit.h history.h util.h
history.o:  history.c history.h

test: all
	bash tests/run_tests.sh

asan: clean
	$(MAKE) EXTRA_CFLAGS="-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined"

clean:
	rm -f $(SERVER) $(CLIENT) *.o

.PHONY: all test asan clean
