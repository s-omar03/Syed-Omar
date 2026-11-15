/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : client.c
 *           Descriptions   : Client side of tftp protocol: an interactive prompt ("tftp] ")
 *                            with the commands connect, get, put, mode, help and bye/quit
 *           Objective      : To send/receive files to/from a TFTP server
 *           Usage          : ./tftp_client [server-ip [port]]
 *                            then type commands at the prompt, e.g.
 *                              connect 127.0.0.1   get syslog   put README   mode octet   bye
 *           Output         : INFO:/ERROR: lines, a '#' progress bar during transfers
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* getline(), sigaction() */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/in.h>
#include "file.h"
#include "udp.h"
#include "tftp.h"

#define CLIENT_NAME         "tftp_client"
#define PROMPT              "tftp] "
#define MAX_TOKENS          4           /* Command word + up to 3 arguments          */
#define TOKEN_SEPARATORS    " \t\r\n"
#define PROGRESS_BLOCKS     16          /* One '#' per 16 blocks (8 KB of data)      */
#define PROGRESS_PER_LINE   64          /* '#' marks per output line                 */
#define TEMP_PATH_MAX       (FILE_PATH_MAX + 64)
#define DEFAULT_MODE        TFTP_MODE_NETASCII
#define CMD_CONTINUE        0
#define CMD_EXIT            1
#define HELP_WIDTH          28          /* Column of the ":" in the help text       */

/* What the client knows between commands */
typedef struct client_state
{
    int connected;                  /* 1 after a successful "connect"              */
    struct sockaddr_in server;      /* Server address and request (listen) port    */
    tftp_mode_t mode;               /* Mode for the next transfers                 */
} client_state_t;

/* Progress bar of one transfer */
typedef struct progress_state
{
    unsigned long marks;            /* '#' printed so far */
} progress_state_t;

typedef int (*command_fn)(client_state_t *st, int argc, char *argv[]);

/* One entry of the command table */
typedef struct command
{
    const char *name;
    int min_args;                   /* Arguments after the command word */
    int max_args;
    command_fn run;
    const char *usage;
    const char *help;
} command_t;

static volatile sig_atomic_t g_interrupted;    /* Set by Ctrl-C */

static int cmd_connect(client_state_t *st, int argc, char *argv[]);
static int cmd_get(client_state_t *st, int argc, char *argv[]);
static int cmd_put(client_state_t *st, int argc, char *argv[]);
static int cmd_mode(client_state_t *st, int argc, char *argv[]);
static int cmd_help(client_state_t *st, int argc, char *argv[]);
static int cmd_bye(client_state_t *st, int argc, char *argv[]);

static const command_t commands[] =
{
    { "connect", 1, 2, cmd_connect, "connect <server-ip> [port]", "Connect to server" },
    { "get",     1, 2, cmd_get,     "get <file-name> [local]",    "Receive a file from server" },
    { "put",     1, 2, cmd_put,     "put <file-name> [remote]",   "Send a file to the server" },
    { "mode",    0, 1, cmd_mode,    "mode [octet|netascii]",
      "Transfer mode(s) (octet, netascii; mail is obsolete)" },
    { "help",    0, 0, cmd_help,    "help",                       "Show this list" },
    { "bye",     0, 0, cmd_bye,     "bye / quit",                 "Exit the application" },
    { "quit",    0, 0, cmd_bye,     NULL,                         NULL }
};

#define COMMAND_COUNT   (sizeof commands / sizeof commands[0])

/*
 * Function    : on_sigint
 * Description : Ctrl-C handler: cancels a running transfer, or ends the
 *               client when it waits at the prompt
 */
static void on_sigint(int sig)
{
    (void)sig;
    g_interrupted = 1;
}

/*
 * Function    : install_sigint
 * Description : Installs on_sigint without SA_RESTART, so that a blocked
 *               poll() or getline() returns and notices the flag
 * Return      : 0 on success, -1 on error
 */
static int install_sigint(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    if (sigemptyset(&sa.sa_mask) != 0)
        return -1;
    sa.sa_flags = 0;
    return sigaction(SIGINT, &sa, NULL);
}

/*
 * Function    : base_name
 * Description : Last component of a path ("dir/file" -> "file")
 */
static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');

    return (slash != NULL) ? slash + 1 : path;
}

/*
 * Function    : valid_leaf
 * Description : A file name usable on its own: not empty, not "." or ".."
 */
static int valid_leaf(const char *name)
{
    return name[0] != '\0' && strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

/*
 * Function    : print_mark
 * Description : Prints one '#' of the progress bar
 */
static void print_mark(progress_state_t *p)
{
    putchar('#');
    p->marks++;
    if (p->marks % PROGRESS_PER_LINE == 0)
        putchar('\n');
    fflush(stdout);
}

/*
 * Function    : on_progress
 * Description : Transfer engine callback: one '#' every PROGRESS_BLOCKS blocks
 */
static void on_progress(void *ctx, unsigned long long blocks)
{
    if (blocks % PROGRESS_BLOCKS == 0)
        print_mark(ctx);
}

/*
 * Function    : progress_end
 * Description : Finishes the progress bar line (a successful transfer always
 *               shows at least one '#')
 */
static void progress_end(progress_state_t *p, int success)
{
    if (success && p->marks == 0)
        print_mark(p);
    if (p->marks % PROGRESS_PER_LINE != 0)
        putchar('\n');
    fflush(stdout);
}

/*
 * Function    : report_failure
 * Description : Prints the reason of a failed transfer. The two most common
 *               server errors use the wording of the project's sample output.
 */
static void report_failure(int rc, const tftp_session_t *s)
{
    char text[PACKETSIZE];
    size_t len;

    if (rc == TFTP_ERR_REMOTE && s->ecode == ECODE_1)
    {
        printf("ERROR: File not found\n");
        return;
    }
    if (rc == TFTP_ERR_REMOTE && s->ecode == ECODE_6)
    {
        printf("ERROR: File already exist\n");
        return;
    }
    if (rc == TFTP_ERR_TIMEOUT)
    {
        printf("ERROR: Server not responding (transfer timed out)\n");
        return;
    }

    tftp_printable(s->message, text, sizeof text);
    len = strlen(text);
    if (len > 0 && text[len - 1] == '.')
        text[len - 1] = '\0';
    printf("ERROR: %s\n", (text[0] != '\0') ? text : tftp_result_string(rc));
}

/*
 * Function    : start_session
 * Description : Opens a fresh socket (new TID) for one transfer and prepares
 *               the session with Ctrl-C cancellation and the progress bar
 * Return      : 0 on success, -1 on error (message printed)
 */
static int start_session(const client_state_t *st, tftp_session_t *s, progress_state_t *p)
{
    int sock = udp_bind_client();

    if (sock < 0)
    {
        printf("ERROR: Cannot create socket: %s\n", strerror(errno));
        return -1;
    }
    tftp_session_init(s, sock, &st->server, 0);
    g_interrupted = 0;
    s->cancel = &g_interrupted;
    p->marks = 0;
    s->progress = on_progress;
    s->progress_ctx = p;
    return 0;
}

/*
 * Function    : cmd_connect
 * Description : "connect <server-ip> [port]": remembers the server address.
 *               UDP has no connection, so nothing is sent yet.
 */
static int cmd_connect(client_state_t *st, int argc, char *argv[])
{
    struct sockaddr_in addr;
    unsigned short port = TFTP_DEFAULT_PORT;
    int rc;

    if (argc > 2 && udp_parse_port(argv[2], &port) != 0)
    {
        printf("ERROR: Invalid port '%s' (1-%d)\n", argv[2], UDP_PORT_MAX);
        return CMD_CONTINUE;
    }
    rc = udp_resolve(argv[1], port, &addr);
    if (rc != 0)
    {
        printf("ERROR: Invalid server address '%s': %s\n", argv[1], udp_resolve_error(rc));
        return CMD_CONTINUE;
    }
    st->server = addr;
    st->connected = 1;
    printf("INFO: Connected to server\n");
    return CMD_CONTINUE;
}

/*
 * Function    : cmd_get
 * Description : "get <file> [local]": downloads a file. It is received into
 *               a temporary file that replaces the local file only when the
 *               transfer succeeded.
 */
static int cmd_get(client_state_t *st, int argc, char *argv[])
{
    const char *remote = argv[1];
    const char *local = (argc > 2) ? argv[2] : base_name(remote);
    char temp[TEMP_PATH_MAX];
    tftp_session_t session;
    tftp_file_t file;
    progress_state_t progress;
    struct stat sb;
    int rc;

    if (!st->connected)
    {
        printf("ERROR: Not connected to server\n");
        return CMD_CONTINUE;
    }
    if (!valid_leaf(base_name(local)))
    {
        printf("ERROR: Invalid local file name '%s'\n", local);
        return CMD_CONTINUE;
    }
    printf("INFO: File: %s\n", remote);
    printf("INFO: Transfer Mode: %s\n", tftp_mode_name(st->mode));
    fflush(stdout);

    if (stat(local, &sb) == 0 && S_ISDIR(sb.st_mode))
    {
        printf("ERROR: '%s' is a directory\n", local);
        return CMD_CONTINUE;
    }
    if (file_create_temp(&file, local, temp, sizeof temp, st->mode == TFTP_MODE_NETASCII) != 0)
    {
        printf("ERROR: Cannot create local file '%s': %s\n", local, strerror(errno));
        return CMD_CONTINUE;
    }
    if (start_session(st, &session, &progress) != 0)
    {
        file_close(&file);
        unlink(temp);
        return CMD_CONTINUE;
    }

    rc = tftp_send_request(&session, OPCODE_RRQ, remote, st->mode);
    if (rc == TFTP_OK)
        rc = tftp_receive_file(&session, &file);
    progress_end(&progress, rc == TFTP_OK);
    file_close(&file);
    close(session.sock);

    if (rc != TFTP_OK)
    {
        if (unlink(temp) != 0)
            printf("WARNING: Cannot remove '%s': %s\n", temp, strerror(errno));
        report_failure(rc, &session);
        return CMD_CONTINUE;
    }
    if (rename(temp, local) != 0)
    {
        printf("ERROR: Cannot save '%s': %s\n", local, strerror(errno));
        if (unlink(temp) != 0)
            printf("WARNING: Cannot remove '%s': %s\n", temp, strerror(errno));
        return CMD_CONTINUE;
    }
    printf("INFO: File receive successful\n");
    return CMD_CONTINUE;
}

/*
 * Function    : cmd_put
 * Description : "put <file> [remote]": uploads a local file. The name on the
 *               server is the last component of the local path unless a
 *               remote name is given.
 */
static int cmd_put(client_state_t *st, int argc, char *argv[])
{
    const char *local = argv[1];
    const char *remote = (argc > 2) ? argv[2] : base_name(local);
    tftp_session_t session;
    tftp_file_t file;
    progress_state_t progress;
    int rc;

    if (!st->connected)
    {
        printf("ERROR: Not connected to server\n");
        return CMD_CONTINUE;
    }
    if (!valid_leaf(base_name(remote)))
    {
        printf("ERROR: Invalid remote file name '%s'\n", remote);
        return CMD_CONTINUE;
    }
    if (file_open_read(&file, local, st->mode == TFTP_MODE_NETASCII) != 0)
    {
        printf("ERROR: Cannot open local file '%s': %s\n", local, strerror(errno));
        return CMD_CONTINUE;
    }
    printf("INFO: File: %s\n", remote);
    printf("INFO: Transfer Mode: %s\n", tftp_mode_name(st->mode));
    fflush(stdout);

    if (start_session(st, &session, &progress) != 0)
    {
        file_close(&file);
        return CMD_CONTINUE;
    }
    rc = tftp_send_request(&session, OPCODE_WRQ, remote, st->mode);
    if (rc == TFTP_OK)
        rc = tftp_send_file(&session, &file);
    progress_end(&progress, rc == TFTP_OK);
    file_close(&file);
    close(session.sock);

    if (rc != TFTP_OK)
        report_failure(rc, &session);
    else
        printf("INFO: File transfer successful\n");
    return CMD_CONTINUE;
}

/*
 * Function    : cmd_mode
 * Description : "mode [octet|netascii]": shows or sets the transfer mode.
 *               "mail" is recognised but refused: RFC 1350 calls it obsolete.
 */
static int cmd_mode(client_state_t *st, int argc, char *argv[])
{
    tftp_mode_t mode;

    if (argc > 1)
    {
        mode = tftp_parse_mode(argv[1]);
        if (mode == TFTP_MODE_MAIL)
        {
            printf("ERROR: Mode 'mail' is obsolete (RFC 1350) and not supported; "
                   "use octet or netascii\n");
            return CMD_CONTINUE;
        }
        if (mode == TFTP_MODE_INVALID)
        {
            printf("ERROR: Unknown mode '%s'; use octet or netascii\n", argv[1]);
            return CMD_CONTINUE;
        }
        st->mode = mode;
    }
    printf("INFO: Transfer Mode: %s\n", tftp_mode_name(st->mode));
    return CMD_CONTINUE;
}

/*
 * Function    : cmd_help
 * Description : "help": lists the commands
 */
static int cmd_help(client_state_t *st, int argc, char *argv[])
{
    size_t i;

    (void)st;
    (void)argc;
    (void)argv;
    for (i = 0; i < COMMAND_COUNT; i++)
    {
        if (commands[i].usage != NULL)
            printf("%-*s: %s\n", HELP_WIDTH, commands[i].usage, commands[i].help);
    }
    printf("Default server port is %d, default mode is %s.\n",
           TFTP_DEFAULT_PORT, tftp_mode_name(DEFAULT_MODE));
    return CMD_CONTINUE;
}

/*
 * Function    : cmd_bye
 * Description : "bye" / "quit": leaves the client
 */
static int cmd_bye(client_state_t *st, int argc, char *argv[])
{
    (void)st;
    (void)argc;
    (void)argv;
    return CMD_EXIT;
}

/*
 * Function    : run_command
 * Description : Splits a command line into words and runs the command
 * Return      : CMD_EXIT to leave the client, CMD_CONTINUE otherwise
 */
static int run_command(client_state_t *st, char *line)
{
    char *argv[MAX_TOKENS + 1];
    char *word, *save = NULL;
    int argc = 0;
    size_t i;

    for (word = strtok_r(line, TOKEN_SEPARATORS, &save); word != NULL;
         word = strtok_r(NULL, TOKEN_SEPARATORS, &save))
    {
        if (argc == MAX_TOKENS)
        {
            printf("ERROR: Too many arguments\n");
            return CMD_CONTINUE;
        }
        argv[argc++] = word;
    }
    argv[argc] = NULL;
    if (argc == 0)
        return CMD_CONTINUE;            /* Empty line */

    for (i = 0; i < COMMAND_COUNT; i++)
    {
        const command_t *cmd = &commands[i];

        if (strcmp(argv[0], cmd->name) != 0)
            continue;
        if (argc - 1 < cmd->min_args || argc - 1 > cmd->max_args)
        {
            printf("ERROR: Usage: %s\n", (cmd->usage != NULL) ? cmd->usage : cmd->name);
            return CMD_CONTINUE;
        }
        return cmd->run(st, argc, argv);
    }
    printf("Invalid command\n");
    return CMD_CONTINUE;
}

/*
 * Function    : print_usage
 * Description : Command line summary
 */
static void print_usage(FILE *out, const char *prog)
{
    fprintf(out,
            "Usage: %s [server-ip [port]]\n"
            "Starts an interactive TFTP client; type 'help' at the prompt.\n"
            "With a server address it connects at once (default port %d).\n",
            prog, TFTP_DEFAULT_PORT);
}

/*
 * Function    : main
 * Description : Optional connect from the command line, then the prompt loop
 *               until bye/quit, end of input or Ctrl-C at the prompt
 */
int main(int argc, char *argv[])
{
    const char *prog = (argc > 0 && argv[0] != NULL) ? argv[0] : CLIENT_NAME;
    client_state_t st;
    char *line = NULL;
    size_t capacity = 0;
    int echo = !isatty(STDIN_FILENO);

    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))
    {
        print_usage(stdout, prog);
        return EXIT_SUCCESS;
    }
    if (argc > 3)
    {
        print_usage(stderr, prog);
        return EXIT_FAILURE;
    }
    if (install_sigint() != 0)
    {
        fprintf(stderr, "%s: cannot install signal handler: %s\n", prog, strerror(errno));
        return EXIT_FAILURE;
    }

    memset(&st, 0, sizeof st);
    st.connected = 0;
    st.mode = DEFAULT_MODE;

    printf("Type help to get supported features\n");
    if (argc > 1)
    {
        char *connect_argv[MAX_TOKENS + 1] = { "connect", argv[1], NULL, NULL, NULL };

        connect_argv[2] = (argc > 2) ? argv[2] : NULL;
        cmd_connect(&st, argc, connect_argv);
    }

    for (;;)
    {
        ssize_t n;

        printf("%s", PROMPT);
        fflush(stdout);
        g_interrupted = 0;
        n = getline(&line, &capacity, stdin);
        if (n < 0)
        {
            putchar('\n');          /* End of input or Ctrl-C: leave like "bye" */
            break;
        }
        if (n > 0 && line[n - 1] == '\n')
            line[n - 1] = '\0';
        if (echo)
            printf("%s\n", line);   /* Input is a file or pipe: show the command */
        if (run_command(&st, line) == CMD_EXIT)
            break;
    }
    free(line);
    return EXIT_SUCCESS;
}
