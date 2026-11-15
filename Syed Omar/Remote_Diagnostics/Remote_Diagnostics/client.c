/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : client.c
 *           Descriptions   : Remote management client: connects to the server, shows the
 *                            "Client] " prompt, runs help/history locally and sends the
 *                            get-mem, get-cpu-usage, get-ports-used, get-open-fd and kill
 *                            commands to the server
 *           Objective      : Remote diagnostics of the processes on the server machine
 *           Usage          : ./client [server-ip [port]]      (default 127.0.0.1 6330)
 *           Output         : INFO: Connected to the Server
 *                            Type help to get supported commands
 *                            Client] get-mem firefox
 *                            INFO: Message sent to the Server
 *                            INFO: Message accepted by the Server
 *                            RESPONSE: "<reply text>"
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* sigaction(), getaddrinfo(), poll() */

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include "history.h"
#include "lineedit.h"
#include "protocol.h"
#include "util.h"

#define PROMPT                "Client] "
#define CONNECT_TIMEOUT_MS    5000            /* Give up connecting after 5 s          */
#define HANDSHAKE_TIMEOUT_MS  5000            /* Server must greet within 5 s          */
#define REPLY_TIMEOUT_MS      30000           /* Server must send each frame in 30 s   */
#define CLIENT_MAX_REPLY      (1024 * 1024)   /* Longest reply accepted                */
#define MAX_TOKENS            4               /* Words looked at in a command line     */
#define HELP_COLUMN           32              /* Width of the usage column of "help"   */
#define PORT_TEXT_MAX         8
#define REASON_TEXT_MAX       256
#define ASCII_FIRST           0x20
#define ASCII_LAST            0x7e
#define SIGNAL_CHOICES        "TERM (default), KILL, INT, HUP or QUIT"

/* What the main loop does after a command */
typedef enum
{
    SESSION_CONTINUE,       /* show the prompt again          */
    SESSION_QUIT,           /* the user wants to leave        */
    SESSION_LOST            /* the connection is unusable     */
} SessionAction;

/* One line of the help text */
typedef struct
{
    const char *usage;
    const char *text;
} HelpEntry;

/* The first seven lines are the ones of the spec (Fig 5.3) */
static const HelpEntry HELP[] =
{
    { "get-mem <process-name>",        "Gets the memory usage of given process" },
    { "get-cpu-usage <process-name>",  "Gets the CPU usage of given process" },
    { "get-ports-used <process-name>", "Fetch the socket ports used by given process" },
    { "get-open-fd <process-name>",    "Fetch the open file descriptors of a given process" },
    { "kill <process-name>",           "Kill the given process" },
    { "history",                       "Show commands history" },
    { "help",                          "Shows this output" },
    { "kill <process-name> <signal>",  "Send " SIGNAL_CHOICES },
    { "exit / quit / Ctrl+D",          "Close the connection and exit" },
    { "Up / Down arrow keys",          "Recall previous commands" }
};

#define HELP_COUNT (sizeof HELP / sizeof HELP[0])

/* Usage of each remote command, indexed by CommandCode */
static const char *const USAGE[CMD_LAST + 1] =
{
    NULL,
    "get-mem <process-name>",
    "get-cpu-usage <process-name>",
    "get-ports-used <process-name>",
    "get-open-fd <process-name>",
    "kill <process-name> [TERM|KILL|INT|HUP|QUIT]"
};

static volatile sig_atomic_t g_quit = 0;    /* set by SIGINT/SIGTERM/SIGHUP/SIGQUIT */
static History g_history;                   /* command history (50 entries)        */

/*
 * Function    : on_quit_signal
 * Description : Signal handler: only sets a flag. The blocking calls return
 *               with EINTR (no SA_RESTART) and the main loop then restores
 *               the terminal, saves the history and exits.
 */
static void on_quit_signal(int sig)
{
    (void)sig;
    g_quit = 1;
}

/*
 * Function    : install_signals
 * Description : Ignores SIGPIPE and routes the terminating signals to
 *               on_quit_signal()
 * Return      : 0 on success, -1 on error
 */
static int install_signals(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, NULL) != 0)
        return -1;
    sa.sa_handler = on_quit_signal;
    sa.sa_flags = 0;                        /* no SA_RESTART: interrupt blocking calls */
    if (sigaction(SIGINT, &sa, NULL) != 0 || sigaction(SIGTERM, &sa, NULL) != 0
        || sigaction(SIGHUP, &sa, NULL) != 0 || sigaction(SIGQUIT, &sa, NULL) != 0)
        return -1;
    return 0;
}

/*
 * Function    : print_usage
 * Description : Prints the command line summary
 */
static void print_usage(FILE *out, const char *prog)
{
    fprintf(out,
            "Usage: %s [server-ip [port]]\n"
            "  server-ip  address of the machine running ./server (default %s)\n"
            "  port       port the server listens on (default %d)\n",
            prog, DEFAULT_SERVER_IP, DEFAULT_PORT);
}

/*
 * Function    : print_help
 * Description : The "help" command (client-only)
 */
static void print_help(void)
{
    size_t i;

    for (i = 0; i < HELP_COUNT; i++)
        printf("%-*s: %s\n", HELP_COLUMN, HELP[i].usage, HELP[i].text);
}

/*
 * Function    : print_sanitized
 * Description : Prints text received from the network. Anything except
 *               printable ASCII, newline and tab becomes '?', so a server
 *               can never send terminal control sequences.
 */
static void print_sanitized(const unsigned char *text, size_t length)
{
    size_t i;

    for (i = 0; i < length; i++)
    {
        unsigned char c = text[i];

        if ((c >= ASCII_FIRST && c <= ASCII_LAST) || c == '\n' || c == '\t')
            putchar(c);
        else
            putchar('?');
    }
}

/*
 * Function    : wait_connected
 * Description : Waits for a non-blocking connect() to finish
 * Return      : 0 when connected, -1 with *err set otherwise
 */
static int wait_connected(int fd, int *err)
{
    long long deadline = util_now_ms() + CONNECT_TIMEOUT_MS;

    for (;;)
    {
        struct pollfd pfd;
        long long left = deadline - util_now_ms();
        int rc, so_error = 0;
        socklen_t len = sizeof so_error;

        if (left <= 0)
        {
            *err = ETIMEDOUT;
            return -1;
        }
        pfd.fd = fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        rc = poll(&pfd, 1, (int)left);
        if (rc < 0)
        {
            if (errno == EINTR && !g_quit)
                continue;
            *err = errno;
            return -1;
        }
        if (rc == 0)
            continue;                       /* re-check the deadline */
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) != 0)
        {
            *err = errno;
            return -1;
        }
        if (so_error != 0)
        {
            *err = so_error;
            return -1;
        }
        return 0;
    }
}

/*
 * Function    : connect_to_server
 * Description : Resolves host:port (IPv4, IPv6 or a host name) and connects
 *               with a timeout. The socket stays non-blocking.
 * Return      : The socket, or -1 with a reason in 'why'
 */
static int connect_to_server(const char *host, unsigned short port, char *why, size_t why_size)
{
    struct addrinfo hints, *res = NULL, *ai;
    char port_text[PORT_TEXT_MAX];
    int fd = -1, rc, err = ECONNREFUSED;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;
    snprintf(port_text, sizeof port_text, "%u", (unsigned)port);
    rc = getaddrinfo(host, port_text, &hints, &res);
    if (rc != 0)
    {
        snprintf(why, why_size, "%s", gai_strerror(rc));
        return -1;
    }
    for (ai = res; ai != NULL; ai = ai->ai_next)
    {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
        {
            err = errno;
            continue;
        }
        if (proto_set_nonblocking(fd) != 0)
            err = errno;
        else if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        else if (errno != EINPROGRESS)
            err = errno;
        else if (wait_connected(fd, &err) == 0)
            break;
        close(fd);
        fd = -1;
        if (g_quit)
            break;
    }
    freeaddrinfo(res);
    if (fd < 0)
        snprintf(why, why_size, "%s", g_quit ? "interrupted" : strerror(err));
    return fd;
}

/*
 * Function    : handshake
 * Description : Waits for the server's HELLO. A busy server sends a NACK
 *               with the reason instead.
 * Return      : 0 on success, -1 after printing an error
 */
static int handshake(int fd)
{
    Frame frame;
    int rc = proto_recv(fd, &frame, HANDSHAKE_TIMEOUT_MS);

    if (rc != PROTO_OK)
    {
        fprintf(stderr, "ERROR: No greeting from the Server (%s)\n",
                rc == PROTO_ERR_IO ? strerror(errno) : proto_result_text(rc));
        return -1;
    }
    if (frame.type == MSG_NACK)
    {
        char reason[REASON_TEXT_MAX];
        size_t i, n = frame.length < sizeof reason - 1 ? frame.length : sizeof reason - 1;

        for (i = 0; i < n; i++)
        {
            unsigned char c = frame.payload[i];

            reason[i] = (char)(c >= ASCII_FIRST && c <= ASCII_LAST ? c : '?');
        }
        reason[n] = '\0';
        fprintf(stderr, "ERROR: The Server refused the connection: %s\n", reason);
        return -1;
    }
    if (frame.type != MSG_HELLO)
    {
        fprintf(stderr, "ERROR: Unexpected greeting from the Server\n");
        return -1;
    }
    return 0;
}

/*
 * Function    : connection_problem
 * Description : Reports a failed send/receive
 * Return      : SESSION_QUIT if the user interrupted, SESSION_LOST otherwise
 */
static int connection_problem(int rc, int saved_errno)
{
    if (rc == PROTO_ERR_INTERRUPTED)
        return SESSION_QUIT;
    printf("ERROR: Connection to the Server lost (%s)\n",
           rc == PROTO_ERR_IO ? strerror(saved_errno) : proto_result_text(rc));
    return SESSION_LOST;
}

/*
 * Function    : receive_reply
 * Description : Prints the DATA frames as RESPONSE: "<text>" until END
 *               arrives, then the status if it is not "success"
 * Return      : SessionAction
 */
static int receive_reply(int fd, Frame *frame)
{
    size_t total = 0;
    int started = 0;

    for (;;)
    {
        int rc = proto_recv(fd, frame, REPLY_TIMEOUT_MS);
        int saved_errno = errno;

        if (rc != PROTO_OK)
        {
            if (started)
                fputs("\"\n", stdout);
            return connection_problem(rc, saved_errno);
        }
        if (frame->type == MSG_DATA)
        {
            total += frame->length;
            if (total > CLIENT_MAX_REPLY)
            {
                printf("\"\nERROR: The reply from the Server is too large\n");
                return SESSION_LOST;
            }
            if (!started)
                fputs("RESPONSE: \"", stdout);
            started = 1;
            print_sanitized(frame->payload, frame->length);
            continue;
        }
        if (frame->type == MSG_END && frame->length == 1 && frame->payload[0] <= STATUS_LAST)
        {
            int status = frame->payload[0];

            printf("%s\"\n", started ? "" : "RESPONSE: \"");
            if (status != STATUS_OK)
                printf("INFO: Server reported: %s\n", proto_status_text(status));
            return SESSION_CONTINUE;
        }
        if (started)
            fputs("\"\n", stdout);
        printf("ERROR: Unexpected message from the Server\n");
        return SESSION_LOST;
    }
}

/*
 * Function    : execute_remote
 * Description : Sends a request and prints the server's answer:
 *               "Message sent", "Message accepted" (ACK) or "rejected"
 *               (NACK with a reason), then the RESPONSE
 * Return      : SessionAction
 */
static int execute_remote(int fd, const Request *req)
{
    unsigned char payload[PROTO_REQ_FIXED_SIZE + PROTO_MAX_NAME_LEN];
    size_t length = proto_encode_request(req, payload, sizeof payload);
    Frame frame;
    int rc, saved_errno;

    if (length == 0)
    {
        printf("ERROR: Invalid request\n");
        return SESSION_CONTINUE;
    }
    rc = proto_send(fd, MSG_REQUEST, payload, length);
    if (rc != PROTO_OK)
        return connection_problem(rc, errno);
    printf("INFO: Message sent to the Server\n");
    fflush(stdout);

    rc = proto_recv(fd, &frame, REPLY_TIMEOUT_MS);
    saved_errno = errno;
    if (rc != PROTO_OK)
        return connection_problem(rc, saved_errno);
    if (frame.type == MSG_NACK)
    {
        printf("INFO: Message rejected by the Server\nERROR: ");
        print_sanitized(frame.payload, frame.length);
        putchar('\n');
        return SESSION_CONTINUE;
    }
    if (frame.type != MSG_ACK || frame.length != 0)
    {
        printf("ERROR: Unexpected message from the Server\n");
        return SESSION_LOST;
    }
    printf("INFO: Message accepted by the Server\n");
    fflush(stdout);
    return receive_reply(fd, &frame);
}

/*
 * Function    : tokenize
 * Description : Splits the line in place at spaces and tabs. Stores the
 *               first 'max' words in tok[].
 * Return      : Total number of words (may be larger than max)
 */
static size_t tokenize(char *line, char **tok, size_t max)
{
    size_t n = 0;
    char *p = line;

    for (;;)
    {
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            break;
        if (n < max)
            tok[n] = p;
        n++;
        while (*p != '\0' && *p != ' ' && *p != '\t')
            p++;
        if (*p != '\0')
            *p++ = '\0';
    }
    return n;
}

/*
 * Function    : printable_line
 * Description : Command lines may only hold printable ASCII and tabs
 * Return      : 1 if the line is acceptable, 0 otherwise
 */
static int printable_line(const char *line)
{
    for (; *line != '\0'; line++)
    {
        unsigned char c = (unsigned char)*line;

        if ((c < ASCII_FIRST || c > ASCII_LAST) && c != '\t')
            return 0;
    }
    return 1;
}

/*
 * Function    : build_request
 * Description : Checks the arguments of a remote command and fills in the
 *               request. Errors are found here, before anything is sent.
 * Return      : 0 if the request is valid, -1 after printing the problem
 */
static int build_request(int cmd, char **tok, size_t ntok, Request *req)
{
    size_t max_words = cmd == CMD_KILL ? 3 : 2;
    size_t name_len;

    if (ntok < 2)
    {
        printf("ERROR: Missing process name. Usage: %s\n", USAGE[cmd]);
        return -1;
    }
    if (ntok > max_words)
    {
        printf("ERROR: Too many arguments. Usage: %s\n", USAGE[cmd]);
        return -1;
    }
    name_len = strlen(tok[1]);
    if (name_len > PROTO_MAX_NAME_LEN)
    {
        printf("ERROR: Process name too long (at most %u characters)\n",
               (unsigned)PROTO_MAX_NAME_LEN);
        return -1;
    }
    if (!proto_valid_name(tok[1], name_len))
    {
        printf("ERROR: Invalid process name '%s' (use letters, digits and %s; "
               "it must not start with '-'). Usage: %s\n",
               tok[1], PROTO_NAME_EXTRA_CHARS, USAGE[cmd]);
        return -1;
    }
    req->command = cmd;
    req->signal = 0;
    memcpy(req->name, tok[1], name_len + 1);
    if (cmd == CMD_KILL)
    {
        req->signal = PROTO_SIG_DEFAULT;
        if (ntok == 3 && proto_signal_parse(tok[2], &req->signal) != 0)
        {
            printf("ERROR: Unknown signal '%s'. Allowed: %s\n", tok[2], SIGNAL_CHOICES);
            return -1;
        }
    }
    return 0;
}

/*
 * Function    : handle_line
 * Description : Runs one command line. help, history and exit/quit are
 *               handled locally; the remote commands are checked, added to
 *               the history and sent to the server.
 * Return      : SessionAction
 */
static int handle_line(int fd, const char *line)
{
    char work[HISTORY_LINE_MAX], entry[HISTORY_LINE_MAX];
    char *tok[MAX_TOKENS];
    size_t ntok;
    Request req;
    int cmd;

    if (!printable_line(line))
    {
        printf("ERROR: Commands may only contain printable ASCII characters\n");
        return SESSION_CONTINUE;
    }
    snprintf(work, sizeof work, "%s", line);
    ntok = tokenize(work, tok, MAX_TOKENS);
    if (ntok == 0)
        return SESSION_CONTINUE;

    if (strcmp(tok[0], "help") == 0)
    {
        if (ntok > 1)
            printf("ERROR: help takes no arguments\n");
        else
            print_help();
        return SESSION_CONTINUE;
    }
    if (strcmp(tok[0], "history") == 0)
    {
        if (ntok > 1)
            printf("ERROR: history takes no arguments\n");
        else
            history_print(&g_history, stdout);
        return SESSION_CONTINUE;
    }
    if (strcmp(tok[0], "exit") == 0 || strcmp(tok[0], "quit") == 0)
        return SESSION_QUIT;

    cmd = proto_command_from_name(tok[0]);
    if (cmd < 0)
    {
        printf("ERROR: Unknown command '%s'. Type help to get supported commands\n", tok[0]);
        return SESSION_CONTINUE;
    }
    if (build_request(cmd, tok, ntok, &req) != 0)
        return SESSION_CONTINUE;

    /* The history keeps the commands executed on the server (Fig 5.7) */
    if (ntok == 3)
        snprintf(entry, sizeof entry, "%s %s %s", tok[0], tok[1], tok[2]);
    else
        snprintf(entry, sizeof entry, "%s %s", tok[0], tok[1]);
    history_add(&g_history, entry);
    return execute_remote(fd, &req);
}

/*
 * Function    : main
 * Description : Connects, then reads and runs commands until exit, EOF, a
 *               signal or a lost connection. The history is saved on the way
 *               out in every one of these cases.
 */
int main(int argc, char **argv)
{
    const char *host = DEFAULT_SERVER_IP;
    unsigned short port = DEFAULT_PORT;
    char hist_path[HISTORY_PATH_MAX], line[HISTORY_LINE_MAX], why[REASON_TEXT_MAX];
    int fd, action = SESSION_CONTINUE, exit_code = EXIT_SUCCESS;

    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))
    {
        print_usage(stdout, argv[0]);
        return EXIT_SUCCESS;
    }
    if (argc > 3)
    {
        fprintf(stderr, "ERROR: Too many arguments\n");
        print_usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }
    if (argc > 1)
        host = argv[1];
    if (argc > 2 && util_parse_port(argv[2], &port) != 0)
    {
        fprintf(stderr, "ERROR: '%s' is not a valid port number (1-65535)\n", argv[2]);
        print_usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }
    if (install_signals() != 0)
    {
        fprintf(stderr, "ERROR: Cannot install signal handlers (%s)\n", strerror(errno));
        return EXIT_FAILURE;
    }
    proto_set_interrupt_flag(&g_quit);

    history_init(&g_history);
    if (history_default_path(hist_path, sizeof hist_path) != 0)
        hist_path[0] = '\0';
    else if (history_load(&g_history, hist_path) != 0)
        fprintf(stderr, "WARNING: Could not read the history file %s (%s)\n", hist_path,
                strerror(errno));

    fd = connect_to_server(host, port, why, sizeof why);
    if (fd < 0)
    {
        fprintf(stderr, "ERROR: Could not connect to the Server at %s port %u (%s)\n", host,
                (unsigned)port, why);
        return EXIT_FAILURE;
    }
    if (handshake(fd) != 0)
    {
        close(fd);
        return EXIT_FAILURE;
    }
    printf("INFO: Connected to the Server\nType help to get supported commands\n");

    le_init(&g_quit);
    while (action == SESSION_CONTINUE && !g_quit)
    {
        int rc = le_read_line(PROMPT, line, sizeof line, &g_history);

        if (rc == LE_EOF || rc == LE_INTERRUPTED)
            break;
        if (rc == LE_ERROR)
        {
            printf("ERROR: Cannot read the input (%s)\n", strerror(errno));
            exit_code = EXIT_FAILURE;
            break;
        }
        if (rc == LE_TOO_LONG)
        {
            printf("ERROR: Command too long (at most %d characters)\n", HISTORY_LINE_MAX - 1);
            continue;
        }
        action = handle_line(fd, line);
        fflush(stdout);
    }
    le_restore();
    if (action == SESSION_LOST)
        exit_code = EXIT_FAILURE;
    if (g_quit)
    {
        printf("INFO: Interrupted\n");
        exit_code = EXIT_FAILURE;
    }
    if (hist_path[0] != '\0' && history_save(&g_history, hist_path) != 0)
        fprintf(stderr, "WARNING: Could not save the history to %s (%s)\n", hist_path,
                strerror(errno));
    close(fd);
    printf("INFO: Disconnected from the Server\n");
    return exit_code;
}
