/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : server.c
 *           Descriptions   : Server side of tftp protocol. Waits for read/write requests and
 *                            forks one child process per request; the child answers from a
 *                            new port (its transfer ID), starting at 20000 and increased for
 *                            every connection, and transfers the file
 *           Objective      : Ready to receive/send data to clients, forever, in the background
 *           Usage          : ./tftp_server [-p port] [-d directory] [-v] &
 *                            -p  UDP port to listen on (default 6969; 69 needs root)
 *                            -d  directory whose files are served (default: current directory)
 *                            -v  log every request to stderr
 *           Output         : Nothing in normal operation (it runs in the background); with -v,
 *                            one log line per request and per finished transfer
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* sigaction(), getopt(), openat() */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <netinet/in.h>
#include "file.h"
#include "udp.h"
#include "tftp.h"

#define SERVER_NAME             "tftp_server"
#define DEFAULT_DIRECTORY       "."
#define MAX_CHILDREN            64      /* Transfers served at the same time               */
#define POLL_INTERVAL_MS        1000    /* Listening loop wakes up this often to reap      */
#define ERROR_BACKOFF_MS        100     /* Pause after a failed receive (avoids spinning)  */
#define LOG_TEXT_MAX            160     /* Longest file name or mode shown in a log line   */
#define PRIVILEGED_PORT_LIMIT   1024    /* Ports below this need root                      */
#define EXIT_TRANSFER_FAILED    1       /* Exit status of a child whose transfer failed    */

#define MSG_BUSY                "Server busy, try again later"
#define MSG_NO_PORT             "No free transfer port, try again later"
#define MSG_MAIL_MODE           "Mail mode is obsolete and not supported"
#define MSG_BAD_MODE            "Unknown transfer mode"
#define MSG_BAD_REQUEST         "Malformed request"
#define MSG_NOT_REQUEST         "Only read and write requests are accepted on this port"

/* Command line settings */
typedef struct server_config
{
    unsigned short port;            /* Listening port              */
    const char *directory;          /* Served directory            */
    int verbose;                    /* 1: log requests to stderr   */
} server_config_t;

/* State of the listening (parent) process */
typedef struct server_state
{
    int sock;                       /* Listening socket                          */
    int root_fd;                    /* Served directory                          */
    unsigned short next_port;       /* First port to try for the next transfer   */
    unsigned int children;          /* Child processes still running             */
} server_state_t;

static int g_verbose;

/*
 * Function    : log_msg
 * Description : Prints one log line to stderr when -v was given
 */
static void log_msg(const char *format, ...)
{
    va_list args;

    if (!g_verbose)
        return;
    fprintf(stderr, "%s[%ld]: ", SERVER_NAME, (long)getpid());
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

/*
 * Function    : print_usage
 * Description : Prints the command line summary
 */
static void print_usage(FILE *out, const char *prog)
{
    fprintf(out,
            "Usage: %s [-p port] [-d directory] [-v]\n"
            "  -p port       UDP port to listen on (default %d; the standard port %d needs root)\n"
            "  -d directory  Directory whose files are served (default: current directory)\n"
            "  -v            Log every request to stderr\n"
            "  -h            Show this help\n"
            "Run it in the background:  %s &\n",
            prog, TFTP_DEFAULT_PORT, TFTP_STANDARD_PORT, prog);
}

/*
 * Function    : parse_args
 * Description : Reads the command line options into *config
 * Return      : 0 to run the server, 1 if help was printed, -1 on error
 */
static int parse_args(int argc, char *argv[], server_config_t *config)
{
    const char *prog = (argc > 0 && argv[0] != NULL) ? argv[0] : SERVER_NAME;
    int opt;

    config->port = TFTP_DEFAULT_PORT;
    config->directory = DEFAULT_DIRECTORY;
    config->verbose = 0;

    while ((opt = getopt(argc, argv, "p:d:vh")) != -1)
    {
        switch (opt)
        {
        case 'p':
            if (udp_parse_port(optarg, &config->port) != 0)
            {
                fprintf(stderr, "%s: invalid port '%s' (1-%d)\n", prog, optarg, UDP_PORT_MAX);
                return -1;
            }
            break;
        case 'd':
            config->directory = optarg;
            break;
        case 'v':
            config->verbose = 1;
            break;
        case 'h':
            print_usage(stdout, prog);
            return 1;
        default:
            print_usage(stderr, prog);
            return -1;
        }
    }
    if (optind < argc)
    {
        fprintf(stderr, "%s: unexpected argument '%s'\n", prog, argv[optind]);
        print_usage(stderr, prog);
        return -1;
    }
    return 0;
}

/*
 * Function    : on_sigchld
 * Description : SIGCHLD handler. It does nothing itself: its only job is to
 *               interrupt poll() so that the main loop reaps the child at once.
 */
static void on_sigchld(int sig)
{
    (void)sig;
}

/*
 * Function    : set_signal
 * Description : Installs a handler (without SA_RESTART) for one signal
 * Return      : 0 on success, -1 on error
 */
static int set_signal(int sig, void (*handler)(int))
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = handler;
    if (sigemptyset(&sa.sa_mask) != 0)
        return -1;
    sa.sa_flags = (sig == SIGCHLD) ? SA_NOCLDSTOP : 0;
    return sigaction(sig, &sa, NULL);
}

/*
 * Function    : install_signals
 * Description : SIGCHLD wakes the main loop to reap children. SIGXFSZ is
 *               ignored so that exceeding a file size limit makes write()
 *               fail with EFBIG (reported as "disk full") instead of killing
 *               the process. SIGPIPE cannot happen with UDP but is ignored too.
 * Return      : 0 on success, -1 on error
 */
static int install_signals(void)
{
    if (set_signal(SIGCHLD, on_sigchld) != 0)
        return -1;
    if (set_signal(SIGXFSZ, SIG_IGN) != 0)
        return -1;
    if (set_signal(SIGPIPE, SIG_IGN) != 0)
        return -1;
    return 0;
}

/*
 * Function    : reap_children
 * Description : Collects every finished child so no zombies are left
 */
static void reap_children(server_state_t *st)
{
    for (;;)
    {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);

        if (pid > 0)
        {
            if (st->children > 0)
                st->children--;
            continue;
        }
        if (pid < 0 && errno == EINTR)
            continue;
        if (pid < 0 && errno == ECHILD)
            st->children = 0;       /* No children left: the count must be 0 */
        break;                      /* 0: none finished yet */
    }
}

/*
 * Function    : refuse
 * Description : Child side: rejects a request with an ERROR packet
 * Return      : EXIT_TRANSFER_FAILED
 */
static int refuse(int sock, const struct sockaddr_in *client, ecode_t code, const char *message,
                  const char *name)
{
    log_msg("refused \"%s\": %s", name, message);
    (void)tftp_send_error(sock, client, code, message);     /* Best effort */
    return EXIT_TRANSFER_FAILED;
}

/*
 * Function    : serve_request
 * Description : Child side: checks one RRQ/WRQ and performs the transfer
 *               from the child's own socket (new TID). Errors of the initial
 *               checks (bad mode, unsafe name, missing file, file already
 *               exists...) are sent from that socket too. A failed upload is
 *               removed, so no partial file is left behind.
 * Return      : EXIT_SUCCESS or EXIT_TRANSFER_FAILED (the child's exit status)
 */
static int serve_request(int sock, int root_fd, const struct sockaddr_in *client,
                         const packet_t *req)
{
    tftp_session_t session;
    tftp_file_t file;
    file_location_t loc;
    tftp_mode_t mode = tftp_parse_mode(req->mode);
    int writing = IS_WRQ(req->opcode);
    char name[LOG_TEXT_MAX];
    int fd, rc;

    tftp_printable(req->filename, name, sizeof name);

    if (mode == TFTP_MODE_MAIL)
        return refuse(sock, client, ECODE_4, MSG_MAIL_MODE, name);
    if (mode == TFTP_MODE_INVALID)
        return refuse(sock, client, ECODE_4, MSG_BAD_MODE, name);
    if (file_check_name(req->filename) != 0)
        return refuse(sock, client, ECODE_2, ESTRING_2, name);

    fd = file_open_served(root_fd, req->filename, writing, &loc);
    if (fd < 0)
    {
        int err = errno;
        ecode_t code = tftp_errno_to_ecode(err);

        return refuse(sock, client, code, (code == ECODE_0) ? strerror(err) : tftp_error_string(code),
                      name);
    }

    file_attach(&file, fd, mode == TFTP_MODE_NETASCII);
    tftp_session_init(&session, sock, client, 1);
    if (writing)
    {
        session.dally = 1;
        rc = tftp_receive_file(&session, &file);
        file_close(&file);
        if (rc != TFTP_OK && unlinkat(loc.dir_fd, loc.leaf, 0) != 0)
            log_msg("cannot remove incomplete \"%s\": %s", name, strerror(errno));
    }
    else
    {
        rc = tftp_send_file(&session, &file);
        file_close(&file);
    }
    file_location_close(&loc);

    if (rc != TFTP_OK)
    {
        log_msg("transfer of \"%s\" failed (%s): %s", name, tftp_result_string(rc), session.message);
        return EXIT_TRANSFER_FAILED;
    }
    log_msg("%s \"%s\": %llu bytes, %llu blocks, %llu retransmissions",
            writing ? "received" : "sent", name, session.bytes, session.blocks,
            session.retransmissions);
    return EXIT_SUCCESS;
}

/*
 * Function    : run_child
 * Description : Code of a forked child: serves the request and exits
 */
static void run_child(server_state_t *st, int sock, const struct sockaddr_in *client,
                      const packet_t *req)
{
    int rc;

    close(st->sock);
    if (set_signal(SIGCHLD, SIG_DFL) != 0)
        log_msg("cannot reset SIGCHLD: %s", strerror(errno));
    rc = serve_request(sock, st->root_fd, client, req);
    close(sock);
    close(st->root_fd);
    exit(rc);
}

/*
 * Function    : handle_datagram
 * Description : Parent side: validates a packet that arrived on the
 *               listening port. A valid RRQ/WRQ gets a socket on the next
 *               free transfer port (20000, 20001, ...) and a child process
 *               that serves it. Anything else is answered with error 4,
 *               except ERROR packets, which are never answered.
 */
static void handle_datagram(server_state_t *st, const unsigned char *buf, size_t len,
                            const struct sockaddr_in *client)
{
    packet_t req;
    opcode_t opcode;
    char who[UDP_ADDR_TEXT], name[LOG_TEXT_MAX], mode[LOG_TEXT_MAX];
    unsigned short port;
    int parsed, sock;
    pid_t pid;

    udp_endpoint_text(client, who, sizeof who);
    parsed = packet_parse(buf, len, &req);
    if (parsed != 0 || (!IS_RRQ(req.opcode) && !IS_WRQ(req.opcode)))
    {
        if (packet_peek_opcode(buf, len, &opcode) == 0 && IS_ERROR(opcode))
            return;
        log_msg("bad packet (%lu bytes) from %s", (unsigned long)len, who);
        (void)tftp_send_error(st->sock, client, ECODE_4,
                              (parsed != 0) ? MSG_BAD_REQUEST : MSG_NOT_REQUEST);
        return;
    }

    tftp_printable(req.filename, name, sizeof name);
    tftp_printable(req.mode, mode, sizeof mode);
    if (st->children >= MAX_CHILDREN)
    {
        log_msg("busy: refused \"%s\" from %s", name, who);
        (void)tftp_send_error(st->sock, client, ECODE_0, MSG_BUSY);
        return;
    }

    port = st->next_port;
    sock = udp_rebind_server(&port, TFTP_TRANSFER_PORT_FIRST, TFTP_TRANSFER_PORT_LAST);
    if (sock < 0)
    {
        log_msg("no transfer port for %s: %s", who, strerror(errno));
        (void)tftp_send_error(st->sock, client, ECODE_0, MSG_NO_PORT);
        return;
    }
    st->next_port = (port == TFTP_TRANSFER_PORT_LAST) ? TFTP_TRANSFER_PORT_FIRST
                                                      : (unsigned short)(port + 1);
    log_msg("%s \"%s\" mode %s from %s, served on port %u",
            IS_RRQ(req.opcode) ? "RRQ" : "WRQ", name, mode, who, (unsigned int)port);

    fflush(stdout);                 /* Nothing buffered may be written twice */
    fflush(stderr);
    pid = fork();
    if (pid < 0)
    {
        log_msg("fork failed: %s", strerror(errno));
        (void)tftp_send_error(st->sock, client, ECODE_0, MSG_BUSY);
        close(sock);
        return;
    }
    if (pid == 0)
        run_child(st, sock, client, &req);      /* Does not return */

    close(sock);
    st->children++;
}

/*
 * Function    : main
 * Description : Opens the served directory and the listening socket, then
 *               loops forever: reap finished children, wait for a packet,
 *               hand requests to new child processes
 */
int main(int argc, char *argv[])
{
    server_config_t config;
    server_state_t st;
    unsigned char buf[RECV_BUFFER];
    const char *prog = (argc > 0 && argv[0] != NULL) ? argv[0] : SERVER_NAME;
    int rc;

    rc = parse_args(argc, argv, &config);
    if (rc != 0)
        return (rc > 0) ? EXIT_SUCCESS : EXIT_FAILURE;
    g_verbose = config.verbose;

    st.root_fd = open(config.directory, O_RDONLY | O_DIRECTORY);
    if (st.root_fd < 0)
    {
        fprintf(stderr, "%s: cannot open directory '%s': %s\n", prog, config.directory,
                strerror(errno));
        return EXIT_FAILURE;
    }
    st.sock = udp_bind_server(config.port);
    if (st.sock < 0)
    {
        int err = errno;

        fprintf(stderr, "%s: cannot listen on UDP port %u: %s\n", prog,
                (unsigned int)config.port, strerror(err));
        if (err == EACCES && config.port < PRIVILEGED_PORT_LIMIT)
            fprintf(stderr, "%s: ports below %d need root; use -p to pick another port\n",
                    prog, PRIVILEGED_PORT_LIMIT);
        close(st.root_fd);
        return EXIT_FAILURE;
    }
    if (install_signals() != 0)
    {
        fprintf(stderr, "%s: cannot install signal handlers: %s\n", prog, strerror(errno));
        close(st.sock);
        close(st.root_fd);
        return EXIT_FAILURE;
    }
    st.next_port = TFTP_TRANSFER_PORT_FIRST;
    st.children = 0;
    log_msg("serving directory '%s' on UDP port %u", config.directory, (unsigned int)config.port);

    for (;;)
    {
        struct sockaddr_in client;
        ssize_t n;

        reap_children(&st);
        n = udp_receive_packet(st.sock, buf, sizeof buf, &client, POLL_INTERVAL_MS);
        if (n == UDP_TIMEOUT || n == UDP_INTERRUPTED)
            continue;
        if (n < 0)
        {
            log_msg("receive failed: %s", strerror(errno));
            (void)poll(NULL, 0, ERROR_BACKOFF_MS);
            continue;
        }
        handle_datagram(&st, buf, (size_t)n, &client);
    }
}
