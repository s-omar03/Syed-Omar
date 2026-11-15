# TCP/IP chat room - build rules
#
#   make          build ./server and ./client
#   make test     build, then run the automated tests
#   make asan     rebuild with AddressSanitizer + UndefinedBehaviorSanitizer
#   make tsan     rebuild with ThreadSanitizer
#   make clean    remove the binaries and object files (users.db is kept)

CC             = gcc
CFLAGS         = -std=c99 -Wall -Wextra -O2 -pthread $(EXTRA_CFLAGS)
LDFLAGS        = -pthread $(EXTRA_CFLAGS)

SERVER         = server
CLIENT         = client
COMMON_SOURCES = protocol.c tcp.c signal_pipe.c
SERVER_SOURCES = server.c session.c chat_room.c user_db.c logger.c $(COMMON_SOURCES)
CLIENT_SOURCES = client.c client_ui.c $(COMMON_SOURCES)
SERVER_OBJECTS = $(SERVER_SOURCES:.c=.o)
CLIENT_OBJECTS = $(CLIENT_SOURCES:.c=.o)

all: $(SERVER) $(CLIENT)

$(SERVER): $(SERVER_OBJECTS)
	$(CC) $(SERVER_OBJECTS) -o $@ $(LDFLAGS)

$(CLIENT): $(CLIENT_OBJECTS)
	$(CC) $(CLIENT_OBJECTS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

server.o:      server.c chat_room.h user_db.h protocol.h logger.h session.h signal_pipe.h tcp.h
session.o:     session.c session.h chat_room.h user_db.h protocol.h logger.h tcp.h
chat_room.o:   chat_room.c chat_room.h user_db.h protocol.h logger.h
user_db.o:     user_db.c user_db.h protocol.h
logger.o:      logger.c logger.h
client.o:      client.c client_ui.h protocol.h signal_pipe.h tcp.h
client_ui.o:   client_ui.c client_ui.h
protocol.o:    protocol.c protocol.h
tcp.o:         tcp.c tcp.h
signal_pipe.o: signal_pipe.c signal_pipe.h

test: all
	bash tests/run_tests.sh

asan: clean
	$(MAKE) EXTRA_CFLAGS="-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined"

tsan: clean
	$(MAKE) EXTRA_CFLAGS="-g -O1 -fsanitize=thread"

clean:
	rm -f $(SERVER) $(CLIENT) *.o

.PHONY: all test asan tsan clean
