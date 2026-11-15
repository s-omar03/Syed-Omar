/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : server.c
 *           Descriptions   : Remote management server (RMS): listens on a TCP port and forks
 *                            one child process per client (concurrent server)
 *           Objective      : Answer get-mem, get-cpu-usage, get-ports-used, get-open-fd and kill
 *                            requests from remote clients, reading /proc on this machine
 *           Usage          : ./server [port [bind-address]]
 *                            default port 6330, default bind address 127.0.0.1
 *                            ./server 6330 0.0.0.0   accepts clients from other machines
 *           Output         : RMS Server: Server Port Information: 127.0.0.1:6330
 *                            RMS Server: Incoming Remote Request from 127.0.0.1:41235
 *                            ... one activity line per request
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* sigaction(), pselect(), getaddrinfo() */

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "protocol.h"
#include "session.h"
#include "util.h"

#define MAX_CLIENTS          32      /* Clients served at the same time         */
#define LISTEN_BACKLOG       16      /* Pending connections queued by the kernel */
#define ACCEPT_RETRY_MS      100     /* Pause when out of descriptors           */
#define SHUTDOWN_WAIT_MS     2000    /* Time children get to exit on shutdown   */
#define SHUTDOWN_POLL_MS     20
#define PORT_TEXT_MAX        8
#define BUSY_MESSAGE         "Server busy: too many clients, try again later"
#define FORK_FAIL_MESSAGE    "Server could not start a worker process"

/* PIDs of the child processes serving clients */
typedef struct
{
    pid_t pids[MAX_CLIENTS];
    size_t count;
} ChildTable;

static volatile sig_atomic_t g_stop = 0;        /* SIGINT/SIGTERM/SIGHUP received */

/*
 * Function    : on_stop_signal
 * Description : Handler for SIGINT, SIGTERM and SIGHUP: asks the main loop
 *               to shut down (only sets a flag, which is async-signal-safe)
 */
static void on_stop_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

/*
 * Function    : on_child_signal
 * Description : Handler for SIGCHLD. It does nothing itself; its only job is
 *               to interrupt pselect() so the main loop reaps the child.
 */
static void on_child_signal(int sig)
{
    (void)sig;
}

/*
 * Function    : print_usage
 * Description : Prints the command line summary
 */
static void print_usage(FILE *out, const char *prog)
{
    fprintf(out,
            "Usage: %s [port [bind-address]]\n"
            "  port          TCP port to listen on (1-65535, default %d)\n"
            "  bind-address  local IPv4/IPv6 address to listen on (default %s;\n"
            "                use 0.0.0.0 to accept clients from other machines)\n",
            prog, DEFAULT_PORT, DEFAULT_BIND_ADDR);
}

/*
 * Function    : install_signals
 * Description : Installs the handlers and blocks the handled signals. They
 *               are only unblocked inside pselect(), which removes the race
 *               between checking the flags and starting to wait.
 * Return      : 0 on success, -1 on error
 */
static int install_signals(sigset_t *wait_mask)
{
    struct sigaction sa;
    sigset_t block;

    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, NULL) != 0)
        return -1;

    sa.sa_handler = on_stop_signal;
    if (sigaction(SIGINT, &sa, NULL) != 0 || sigaction(SIGTERM, &sa, NULL) != 0
        || sigaction(SIGHUP, &sa, NULL) != 0)
        return -1;

    sa.sa_handler = on_child_signal;
    sa.sa_flags = SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) != 0)
        return -1;

    sigemptyset(&block);
    sigaddset(&block, SIGINT);
    sigaddset(&block, SIGTERM);
    sigaddset(&block, SIGHUP);
    sigaddset(&block, SIGCHLD);
    return sigprocmask(SIG_BLOCK, &block, wait_mask);
}

/*
 * Function    : reset_child_signals
 * Description : In a newly forked child: default handlers for the signals
 *               the parent handles, and the original signal mask
 */
static void reset_child_signals(const sigset_t *orig_mask)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = SIG_DFL;
    (void)sigaction(SIGINT, &sa, NULL);
    (void)sigaction(SIGTERM, &sa, NULL);
    (void)sigaction(SIGHUP, &sa, NULL);
    (void)sigaction(SIGCHLD, &sa, NULL);
    (void)sigprocmask(SIG_SETMASK, orig_mask, NULL);
}

/*
 * Function    : open_listener
 * Description : Creates the listening socket on bind_addr:port (numeric
 *               IPv4 or IPv6 address)
 * Return      : The socket, or -1 after printing an error
 */
static int open_listener(const char *bind_addr, unsigned short port)
{
    struct addrinfo hints, *res = NULL, *ai;
    char port_text[PORT_TEXT_MAX];
    int fd = -1, rc, saved_errno = 0, one = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV;
    snprintf(port_text, sizeof port_text, "%u", (unsigned)port);
    rc = getaddrinfo(bind_addr, port_text, &hints, &res);
    if (rc != 0)
    {
        fprintf(stderr, "Error: '%s' is not a valid IP address to listen on (%s).\n",
                bind_addr, gai_strerror(rc));
        return -1;
    }
    for (ai = res; ai != NULL; ai = ai->ai_next)
    {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
        {
            saved_errno = errno;
            continue;
        }
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) == 0
            && bind(fd, ai->ai_addr, ai->ai_addrlen) == 0
            && listen(fd, LISTEN_BACKLOG) == 0
            && proto_set_nonblocking(fd) == 0
            && fcntl(fd, F_SETFD, FD_CLOEXEC) == 0)
            break;
        saved_errno = errno;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0)
        fprintf(stderr, "Error: Cannot listen on %s port %u (%s).\n", bind_addr,
                (unsigned)port, strerror(saved_errno));
    return fd;
}

/*
 * Function    : reap_children
 * Description : Collects every child that has exited (no zombies are left)
 *               and removes it from the table
 */
static void reap_children(ChildTable *table)
{
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        size_t i;

        for (i = 0; i < table->count; i++)
        {
            if (table->pids[i] == pid)
            {
                table->pids[i] = table->pids[--table->count];
                break;
            }
        }
    }
}

/*
 * Function    : refuse_client
 * Description : Tells a client that it cannot be served, then closes. The
 *               client has not sent anything yet, so closing at once is safe.
 */
static void refuse_client(int fd, const char *reason)
{
    if (proto_set_nonblocking(fd) == 0)
        (void)proto_send(fd, MSG_NACK, reason, strlen(reason));
    close(fd);
}

/*
 * Function    : serve_in_child
 * Description : Body of the child process: serves one client, then exits
 */
static void serve_in_child(int listen_fd, int client_fd, const char *peer,
                           const sigset_t *orig_mask)
{
    int one = 1;

    reset_child_signals(orig_mask);
    close(listen_fd);
    if (proto_set_nonblocking(client_fd) != 0)
    {
        server_log("[%s] Cannot configure the connection: %s", peer, strerror(errno));
        close(client_fd);
        exit(EXIT_FAILURE);
    }
    /* Detect clients that vanished without closing (best effort) */
    (void)setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof one);
    session_run(client_fd, peer);
    close(client_fd);
    exit(EXIT_SUCCESS);
}

/*
 * Function    : accept_client
 * Description : Accepts one pending connection and forks a child for it
 */
static void accept_client(int listen_fd, ChildTable *table, const sigset_t *orig_mask)
{
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof addr;
    char peer[UTIL_ADDR_TEXT_MAX];
    pid_t pid;
    int fd;

    fd = accept(listen_fd, (struct sockaddr *)&addr, &addr_len);
    if (fd < 0)
    {
        if (errno == EMFILE || errno == ENFILE || errno == ENOBUFS || errno == ENOMEM)
        {
            server_log("Cannot accept a connection: %s", strerror(errno));
            util_sleep_ms(ACCEPT_RETRY_MS);  /* avoid spinning while out of descriptors */
        }
        return;     /* EAGAIN, EINTR, ECONNABORTED, ...: nothing to do */
    }
    util_format_addr((struct sockaddr *)&addr, peer, sizeof peer);
    server_log("Incoming Remote Request from %s", peer);

    if (table->count >= MAX_CLIENTS)
    {
        server_log("[%s] Refused: already serving %d clients", peer, MAX_CLIENTS);
        refuse_client(fd, BUSY_MESSAGE);
        return;
    }
    pid = fork();
    if (pid < 0)
    {
        server_log("[%s] Refused: fork failed: %s", peer, strerror(errno));
        refuse_client(fd, FORK_FAIL_MESSAGE);
        return;
    }
    if (pid == 0)
        serve_in_child(listen_fd, fd, peer, orig_mask);     /* does not return */
    table->pids[table->count++] = pid;
    close(fd);
}

/*
 * Function    : stop_children
 * Description : On shutdown: asks every child to stop (SIGTERM), waits up
 *               to SHUTDOWN_WAIT_MS, then kills the rest and reaps them all
 */
static void stop_children(ChildTable *table)
{
    long long deadline = util_now_ms() + SHUTDOWN_WAIT_MS;
    size_t i;

    for (i = 0; i < table->count; i++)
        (void)kill(table->pids[i], SIGTERM);
    while (table->count > 0 && util_now_ms() < deadline)
    {
        reap_children(table);
        if (table->count > 0)
            util_sleep_ms(SHUTDOWN_POLL_MS);
    }
    for (i = 0; i < table->count; i++)
    {
        (void)kill(table->pids[i], SIGKILL);
        (void)waitpid(table->pids[i], NULL, 0);
    }
    table->count = 0;
}

/*
 * Function    : main
 * Description : Parses the arguments, opens the listening socket and runs
 *               the accept loop until SIGINT/SIGTERM/SIGHUP
 */
int main(int argc, char **argv)
{
    const char *bind_addr = DEFAULT_BIND_ADDR;
    unsigned short port = DEFAULT_PORT;
    struct sockaddr_storage local;
    socklen_t local_len = sizeof local;
    char local_text[UTIL_ADDR_TEXT_MAX];
    ChildTable children;
    sigset_t orig_mask;
    int listen_fd;

    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))
    {
        print_usage(stdout, argv[0]);
        return EXIT_SUCCESS;
    }
    if (argc > 3)
    {
        fprintf(stderr, "Error: Too many arguments.\n");
        print_usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }
    if (argc > 1 && util_parse_port(argv[1], &port) != 0)
    {
        fprintf(stderr, "Error: '%s' is not a valid port number (1-65535).\n", argv[1]);
        print_usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }
    if (argc > 2)
        bind_addr = argv[2];

    if (install_signals(&orig_mask) != 0)
    {
        fprintf(stderr, "Error: Cannot install signal handlers (%s).\n", strerror(errno));
        return EXIT_FAILURE;
    }
    listen_fd = open_listener(bind_addr, port);
    if (listen_fd < 0)
        return EXIT_FAILURE;
    if (listen_fd >= FD_SETSIZE)
    {
        fprintf(stderr, "Error: Listening descriptor too large for select().\n");
        close(listen_fd);
        return EXIT_FAILURE;
    }
    if (getsockname(listen_fd, (struct sockaddr *)&local, &local_len) == 0)
        util_format_addr((struct sockaddr *)&local, local_text, sizeof local_text);
    else
        snprintf(local_text, sizeof local_text, "%s:%u", bind_addr, (unsigned)port);
    server_log("Server Port Information: %s", local_text);

    children.count = 0;
    while (!g_stop)
    {
        fd_set readable;
        int rc;

        reap_children(&children);
        FD_ZERO(&readable);
        FD_SET(listen_fd, &readable);
        rc = pselect(listen_fd + 1, &readable, NULL, NULL, NULL, &orig_mask);
        if (rc < 0)
        {
            if (errno == EINTR)
                continue;               /* SIGCHLD or a stop signal */
            server_log("select failed: %s", strerror(errno));
            break;
        }
        if (rc > 0 && FD_ISSET(listen_fd, &readable))
            accept_client(listen_fd, &children, &orig_mask);
    }

    server_log("Shutting down");
    close(listen_fd);
    stop_children(&children);
    return EXIT_SUCCESS;
}
