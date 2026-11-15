/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : server.c
 *           Descriptions   : Main program of the chat server: command line, listening socket,
 *                            accept loop, and orderly shutdown on SIGINT / SIGTERM
 *           Objective      : Concurrent TCP chat server on port 6333 that runs in the
 *                            background without a user interface (spec 2.1, 2.3)
 *           Usage          : ./server [-v] [port]
 *                              -v     log activity to stderr
 *                              port   TCP port to listen on (default 6333)
 *           Output         : One start-up line and one stop line on stdout, errors (and with
 *                            -v the activity log) on stderr, registered users in users.db
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* nanosleep() */

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "chat_room.h"
#include "logger.h"
#include "protocol.h"
#include "session.h"
#include "signal_pipe.h"
#include "tcp.h"

#define LISTEN_BACKLOG            32            /* pending connections queued by the kernel */
#define CLIENT_SEND_TIMEOUT_SEC   5             /* give up on a client that stops reading   */
#define ACCEPT_RETRY_DELAY_NS     100000000L    /* pause after accept() errors (100 ms)     */
#define POLL_LISTEN               0             /* index of the listening socket in poll()  */
#define POLL_SIGNAL               1             /* index of the signal pipe in poll()       */
#define POLL_COUNT                2

#define ARGS_OK                   0
#define ARGS_HELP                 1
#define ARGS_ERROR                -1

/* Shared server state. Static, so it stays valid for detached threads. */
static ChatRoom room;
static SessionManager sessions;

/*
 * Function    : print_usage
 * Description : Prints the command line summary
 */
static void print_usage(FILE *out, const char *prog)
{
    fprintf(out,
            "Usage: %s [-v] [port]\n"
            "  -v     log connections, logins and message routing to stderr\n"
            "  port   TCP port to listen on (default %d)\n"
            "Registered users are kept in %s in the current directory.\n"
            "Stop the server with Ctrl+C or 'kill <pid>'.\n",
            prog, CHAT_DEFAULT_PORT, USER_DB_FILE);
}

/*
 * Function    : parse_args
 * Description : Reads the optional -v flag and port number
 * Return      : ARGS_OK, ARGS_HELP or ARGS_ERROR
 */
static int parse_args(int argc, char *argv[], int *verbose, uint16_t *port)
{
    int i, have_port = 0;

    *verbose = 0;
    *port = CHAT_DEFAULT_PORT;
    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-v") == 0)
            *verbose = 1;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
            return ARGS_HELP;
        else if (!have_port && argv[i][0] != '-' && tcp_parse_port(argv[i], port) == 0)
            have_port = 1;
        else
        {
            fprintf(stderr, "Error: Invalid argument '%s'.\n", argv[i]);
            return ARGS_ERROR;
        }
    }
    return ARGS_OK;
}

/*
 * Function    : pause_after_error
 * Description : Short sleep so a persistent accept() error (for example no
 *               free descriptors) does not turn the loop into a busy loop
 */
static void pause_after_error(void)
{
    struct timespec delay;

    delay.tv_sec = 0;
    delay.tv_nsec = ACCEPT_RETRY_DELAY_NS;
    nanosleep(&delay, NULL);
}

/*
 * Function    : accept_client
 * Description : Accepts one connection and starts its session thread, or
 *               tells the client that the server is full
 */
static void accept_client(int listen_fd)
{
    char peer[TCP_PEER_NAME_SIZE], err[LOG_ERROR_TEXT_SIZE];
    int fd, rc;

    fd = accept(listen_fd, NULL, NULL);
    if (fd < 0)
    {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNABORTED)
            return;
        log_error("accept() failed: %s", log_errno_text(errno, err, sizeof err));
        pause_after_error();
        return;
    }

    /* Blocking socket with a send timeout: a client that stops reading can
       hold up a sender for at most CLIENT_SEND_TIMEOUT_SEC seconds */
    if (tcp_set_nonblocking(fd, 0) != 0 || tcp_set_send_timeout(fd, CLIENT_SEND_TIMEOUT_SEC) != 0)
    {
        log_error("Could not configure a client socket: %s", log_errno_text(errno, err, sizeof err));
        close(fd);
        return;
    }

    rc = session_start(&sessions, fd);
    if (rc == SESSION_STARTED)
        return;

    tcp_peer_name(fd, peer, sizeof peer);
    if (rc == SESSION_FULL)
    {
        /* No other thread knows this socket yet, so no lock is needed */
        log_info("%s rejected: server is full (%d sessions)", peer, SERVER_MAX_SESSIONS);
        if (proto_send_reply(fd, REPLY_FATAL, REPLY_SERVER_FULL) != PROTO_OK)
            log_info("%s: could not send the 'server is full' reply", peer);
    }
    else
    {
        log_error("Could not start a session for %s: %s", peer,
                  log_errno_text(errno, err, sizeof err));
    }
    close(fd);
}

/*
 * Function    : serve
 * Description : Accept loop. Runs until a stop signal arrives through the
 *               signal pipe or poll() fails.
 */
static void serve(int listen_fd, int signal_fd)
{
    struct pollfd fds[POLL_COUNT];
    char err[LOG_ERROR_TEXT_SIZE];

    for (;;)
    {
        fds[POLL_LISTEN].fd = listen_fd;
        fds[POLL_LISTEN].events = POLLIN;
        fds[POLL_SIGNAL].fd = signal_fd;
        fds[POLL_SIGNAL].events = POLLIN;
        if (poll(fds, POLL_COUNT, -1) < 0)
        {
            if (errno == EINTR)
                continue;
            log_error("poll() failed: %s", log_errno_text(errno, err, sizeof err));
            return;
        }
        if (fds[POLL_SIGNAL].revents != 0)
        {
            log_info("Received signal %d", signal_pipe_drain(signal_fd));
            return;
        }
        if (fds[POLL_LISTEN].revents & POLLIN)
            accept_client(listen_fd);
        else if (fds[POLL_LISTEN].revents & (POLLERR | POLLNVAL))
        {
            log_error("The listening socket failed");
            return;
        }
    }
}

/*
 * Function    : main
 * Description : Loads the users, listens, serves until stopped, then ends
 *               every session and releases all resources
 */
int main(int argc, char *argv[])
{
    static const int stop_signals[] = { SIGINT, SIGTERM };
    const char *prog = (argc > 0 && argv[0] != NULL) ? argv[0] : "server";
    char err[LOG_ERROR_TEXT_SIZE];
    size_t skipped;
    uint16_t port;
    int verbose, listen_fd, signal_fd, rc;

    rc = parse_args(argc, argv, &verbose, &port);
    if (rc != ARGS_OK)
    {
        print_usage(rc == ARGS_HELP ? stdout : stderr, prog);
        return rc == ARGS_HELP ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    log_init(verbose);

    signal_fd = signal_pipe_init(stop_signals, sizeof stop_signals / sizeof stop_signals[0]);
    if (signal_fd < 0)
    {
        fprintf(stderr, "Error: Could not set up signal handling: %s\n",
                log_errno_text(errno, err, sizeof err));
        return EXIT_FAILURE;
    }
    if (room_init(&room, USER_DB_FILE, &skipped) != 0)
    {
        fprintf(stderr, "Error: Could not load %s: %s\n", USER_DB_FILE,
                log_errno_text(errno, err, sizeof err));
        return EXIT_FAILURE;
    }
    if (skipped > 0)
        log_error("Warning: %zu invalid line(s) in %s were ignored", skipped, USER_DB_FILE);
    if (session_manager_init(&sessions, &room) != 0)
    {
        fprintf(stderr, "Error: Could not initialise the sessions: %s\n",
                log_errno_text(errno, err, sizeof err));
        room_destroy(&room);
        return EXIT_FAILURE;
    }

    listen_fd = tcp_listen(port, LISTEN_BACKLOG);
    if (listen_fd < 0 || tcp_set_nonblocking(listen_fd, 1) != 0)
    {
        fprintf(stderr, "Error: Could not listen on port %u: %s\n", (unsigned)port,
                log_errno_text(errno, err, sizeof err));
        if (listen_fd >= 0)
            close(listen_fd);
        session_manager_destroy(&sessions);
        room_destroy(&room);
        return EXIT_FAILURE;
    }

    printf("Chat server listening on port %u (%zu registered users)\n", (unsigned)port,
           room_user_count(&room));
    fflush(stdout);

    serve(listen_fd, signal_fd);

    close(listen_fd);
    log_info("Shutting down, closing %d session(s)", session_count(&sessions));
    if (session_shutdown_all(&sessions, SESSION_SHUTDOWN_WAIT_SEC))
    {
        session_manager_destroy(&sessions);
        room_destroy(&room);
    }
    else
    {
        log_error("Some sessions did not end within %d seconds", SESSION_SHUTDOWN_WAIT_SEC);
    }
    close(signal_fd);
    printf("Server stopped\n");
    return EXIT_SUCCESS;
}
