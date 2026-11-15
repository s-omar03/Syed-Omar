/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : protocol.c
 *           Descriptions   : Framed messages over TCP: header encoding, complete sends and
 *                            receives on non-blocking sockets with timeouts, request
 *                            encoding/decoding and validation of every field
 *           Objective      : One careful implementation of the wire format for both programs
 *           Usage          : Linked into both ./server and ./client
 *           Output         : None (library module)
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* poll(), MSG_NOSIGNAL, strcasecmp() */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "protocol.h"
#include "util.h"

#define DRAIN_CHUNK         512     /* Scratch buffer used by proto_drain()        */
#define SIGNAL_PREFIX       "SIG"   /* Optional prefix of signal names             */
#define SIGNAL_PREFIX_LEN   3
#define SIGNAL_TEXT_MAX     16      /* Longest signal text accepted ("-SIGQUIT")   */

/* Names of the remote commands, indexed by CommandCode */
static const char *const COMMAND_NAMES[CMD_LAST + 1] =
{
    NULL, "get-mem", "get-cpu-usage", "get-ports-used", "get-open-fd", "kill"
};

/* Signals a client may ask for with "kill <name> <signal>" */
typedef struct
{
    int wire;               /* Number used on the wire (PROTO_SIG_xxx) */
    int native;             /* Signal number of this system            */
    const char *name;       /* Name without the "SIG" prefix           */
} SignalInfo;

static const SignalInfo SIGNALS[] =
{
    { PROTO_SIG_HUP,  SIGHUP,  "HUP"  },
    { PROTO_SIG_INT,  SIGINT,  "INT"  },
    { PROTO_SIG_QUIT, SIGQUIT, "QUIT" },
    { PROTO_SIG_KILL, SIGKILL, "KILL" },
    { PROTO_SIG_TERM, SIGTERM, "TERM" }
};

#define SIGNAL_COUNT (sizeof SIGNALS / sizeof SIGNALS[0])

/* Set by the client so that Ctrl+C can abort a blocking wait */
static volatile sig_atomic_t *g_interrupt_flag = NULL;

/* Deadlines used while one frame is being received */
typedef struct
{
    int started;            /* 1 once the first byte of the frame arrived   */
    long long idle_deadline;/* Deadline for the first byte, -1 = none       */
    long long deadline;     /* Deadline for the rest of the frame           */
} RecvClock;

/*
 * Function    : proto_set_interrupt_flag
 * Description : Registers a flag that a signal handler sets. When a wait is
 *               interrupted (EINTR) and the flag is set, the I/O functions
 *               return PROTO_ERR_INTERRUPTED instead of continuing.
 */
void proto_set_interrupt_flag(volatile sig_atomic_t *flag)
{
    g_interrupt_flag = flag;
}

/*
 * Function    : interrupted
 * Description : Tells whether the registered interrupt flag is set
 */
static int interrupted(void)
{
    return g_interrupt_flag != NULL && *g_interrupt_flag != 0;
}

/*
 * Function    : proto_set_nonblocking
 * Description : Puts a socket into non-blocking mode. All socket I/O in this
 *               project waits with poll(), so no call can block forever.
 * Return      : 0 on success, -1 on error (errno set)
 */
int proto_set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);

    if (flags < 0)
        return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return -1;
    return 0;
}

/*
 * Function    : wait_ready
 * Description : Waits until the socket is readable/writable or the deadline
 *               (milliseconds of util_now_ms(), -1 = none) has passed
 * Return      : PROTO_OK, PROTO_ERR_TIMEOUT, PROTO_ERR_INTERRUPTED or PROTO_ERR_IO
 */
static int wait_ready(int fd, short events, long long deadline)
{
    for (;;)
    {
        struct pollfd pfd;
        int timeout = -1, rc;

        if (deadline >= 0)
        {
            long long left = deadline - util_now_ms();

            if (left <= 0)
                return PROTO_ERR_TIMEOUT;
            timeout = left > INT_MAX ? INT_MAX : (int)left;
        }
        pfd.fd = fd;
        pfd.events = events;
        pfd.revents = 0;
        rc = poll(&pfd, 1, timeout);
        if (rc > 0)
            return PROTO_OK;            /* errors/hangups show up in recv()/send() */
        if (rc == 0)
            return PROTO_ERR_TIMEOUT;
        if (errno != EINTR)
            return PROTO_ERR_IO;
        if (interrupted())
            return PROTO_ERR_INTERRUPTED;
    }
}

/*
 * Function    : send_all
 * Description : Sends the whole buffer, handling partial sends, EINTR and a
 *               full socket buffer. Gives up when the peer accepts no data
 *               for PROTO_IO_TIMEOUT_MS. MSG_NOSIGNAL prevents SIGPIPE.
 * Return      : PROTO_OK or an error code
 */
static int send_all(int fd, const unsigned char *buf, size_t length)
{
    size_t done = 0;
    long long deadline = util_now_ms() + PROTO_IO_TIMEOUT_MS;

    while (done < length)
    {
        ssize_t n = send(fd, buf + done, length - done, MSG_NOSIGNAL);

        if (n > 0)
        {
            done += (size_t)n;
            deadline = util_now_ms() + PROTO_IO_TIMEOUT_MS;
            continue;
        }
        if (n < 0 && errno == EINTR)
        {
            if (interrupted())
                return PROTO_ERR_INTERRUPTED;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            int rc = wait_ready(fd, POLLOUT, deadline);

            if (rc != PROTO_OK)
                return rc;
            continue;
        }
        return PROTO_ERR_IO;
    }
    return PROTO_OK;
}

/*
 * Function    : recv_bytes
 * Description : Receives exactly 'length' bytes. The first byte of a frame
 *               may take up to the idle deadline; once it has arrived the
 *               whole rest of the frame must arrive within PROTO_IO_TIMEOUT_MS.
 * Return      : PROTO_OK, PROTO_ERR_CLOSED (EOF before the frame started),
 *               PROTO_ERR_TRUNCATED (EOF inside the frame) or another error
 */
static int recv_bytes(int fd, unsigned char *buf, size_t length, RecvClock *clk)
{
    size_t done = 0;

    while (done < length)
    {
        ssize_t n;
        int rc = wait_ready(fd, POLLIN, clk->started ? clk->deadline : clk->idle_deadline);

        if (rc != PROTO_OK)
            return rc;
        n = recv(fd, buf + done, length - done, 0);
        if (n > 0)
        {
            if (!clk->started)
            {
                clk->started = 1;
                clk->deadline = util_now_ms() + PROTO_IO_TIMEOUT_MS;
            }
            done += (size_t)n;
            continue;
        }
        if (n == 0)
            return clk->started ? PROTO_ERR_TRUNCATED : PROTO_ERR_CLOSED;
        if (errno == EINTR)
        {
            if (interrupted())
                return PROTO_ERR_INTERRUPTED;
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            continue;
        return PROTO_ERR_IO;
    }
    return PROTO_OK;
}

/*
 * Function    : proto_send
 * Description : Sends one frame (header + payload) as a single buffer
 * Return      : PROTO_OK or an error code
 */
int proto_send(int fd, int type, const void *payload, size_t length)
{
    unsigned char frame[PROTO_HEADER_SIZE + PROTO_MAX_PAYLOAD];
    uint16_t magic = htons((uint16_t)PROTO_MAGIC);
    uint32_t net_length;

    if (length > PROTO_MAX_PAYLOAD || type < MSG_FIRST || type > MSG_LAST)
        return PROTO_ERR_LENGTH;
    net_length = htonl((uint32_t)length);
    memcpy(frame, &magic, sizeof magic);
    frame[2] = (unsigned char)PROTO_VERSION;
    frame[3] = (unsigned char)type;
    memcpy(frame + 4, &net_length, sizeof net_length);
    if (length > 0)
        memcpy(frame + PROTO_HEADER_SIZE, payload, length);
    return send_all(fd, frame, PROTO_HEADER_SIZE + length);
}

/*
 * Function    : proto_recv
 * Description : Receives and validates one frame. The header is checked
 *               (magic, version, type, length) before any payload is read,
 *               so an oversized length is rejected without reading it.
 *               idle_timeout_ms limits the wait for the first byte
 *               (PROTO_WAIT_FOREVER = no limit).
 * Return      : PROTO_OK (frame filled in, payload NUL-terminated) or an error
 */
int proto_recv(int fd, Frame *frame, int idle_timeout_ms)
{
    unsigned char header[PROTO_HEADER_SIZE];
    uint16_t magic;
    uint32_t length;
    RecvClock clk;
    int rc;

    clk.started = 0;
    clk.deadline = -1;
    clk.idle_deadline = idle_timeout_ms < 0 ? -1 : util_now_ms() + idle_timeout_ms;

    rc = recv_bytes(fd, header, sizeof header, &clk);
    if (rc != PROTO_OK)
        return rc;

    memcpy(&magic, header, sizeof magic);
    memcpy(&length, header + 4, sizeof length);
    magic = ntohs(magic);
    length = ntohl(length);
    if (magic != PROTO_MAGIC)
        return PROTO_ERR_MAGIC;
    if (header[2] != PROTO_VERSION)
        return PROTO_ERR_VERSION;
    if (header[3] < MSG_FIRST || header[3] > MSG_LAST)
        return PROTO_ERR_TYPE;
    if (length > PROTO_MAX_PAYLOAD)
        return PROTO_ERR_LENGTH;

    frame->type = header[3];
    frame->length = length;
    if (length > 0)
    {
        rc = recv_bytes(fd, frame->payload, length, &clk);
        if (rc != PROTO_OK)
            return rc == PROTO_ERR_CLOSED ? PROTO_ERR_TRUNCATED : rc;
    }
    frame->payload[length] = '\0';
    return PROTO_OK;
}

/*
 * Function    : proto_drain
 * Description : Reads and discards what the peer still sends, for at most
 *               timeout_ms or max_bytes. Used after shutdown(SHUT_WR) before
 *               closing a connection with a protocol error: closing with
 *               unread data would send a TCP reset, and the peer could lose
 *               the error message we just sent.
 */
void proto_drain(int fd, int timeout_ms, size_t max_bytes)
{
    unsigned char scratch[DRAIN_CHUNK];
    long long deadline = util_now_ms() + timeout_ms;
    size_t total = 0;

    while (total < max_bytes)
    {
        ssize_t n;

        if (wait_ready(fd, POLLIN, deadline) != PROTO_OK)
            return;
        n = recv(fd, scratch, sizeof scratch, 0);
        if (n == 0)
            return;
        if (n < 0)
        {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            return;
        }
        total += (size_t)n;
    }
}

/*
 * Function    : proto_result_text
 * Description : Human readable text for a ProtoResult
 */
const char *proto_result_text(int result)
{
    switch (result)
    {
    case PROTO_OK:              return "success";
    case PROTO_ERR_CLOSED:      return "connection closed by peer";
    case PROTO_ERR_TRUNCATED:   return "connection closed in the middle of a message";
    case PROTO_ERR_TIMEOUT:     return "timed out";
    case PROTO_ERR_IO:          return "network error";
    case PROTO_ERR_INTERRUPTED: return "interrupted";
    case PROTO_ERR_MAGIC:       return "bad magic number (not a remote diagnostics message)";
    case PROTO_ERR_VERSION:     return "unsupported protocol version";
    case PROTO_ERR_TYPE:        return "unknown message type";
    case PROTO_ERR_LENGTH:      return "message too large";
    default:                    return "unknown error";
    }
}

/*
 * Function    : proto_encode_request
 * Description : Builds the payload of a MSG_REQUEST: command(1) signal(1)
 *               followed by the process name (no terminating NUL)
 * Return      : Payload length, 0 if the request is invalid or 'size' too small
 */
size_t proto_encode_request(const Request *req, unsigned char *buf, size_t size)
{
    size_t name_len = strlen(req->name);

    if (req->command < CMD_FIRST || req->command > CMD_LAST
        || !proto_valid_name(req->name, name_len)
        || size < PROTO_REQ_FIXED_SIZE + name_len)
        return 0;
    buf[0] = (unsigned char)req->command;
    buf[1] = (unsigned char)req->signal;
    memcpy(buf + PROTO_REQ_FIXED_SIZE, req->name, name_len);
    return PROTO_REQ_FIXED_SIZE + name_len;
}

/*
 * Function    : proto_decode_request
 * Description : Validates a MSG_REQUEST payload and fills in *req. Nothing
 *               coming from the network is trusted: the command code, the
 *               signal and every character of the name are checked.
 * Return      : 0 on success, -1 with *why set to the reason otherwise
 */
int proto_decode_request(const unsigned char *payload, size_t length, Request *req,
                         const char **why)
{
    size_t name_len;

    if (length <= PROTO_REQ_FIXED_SIZE)
    {
        *why = "request has no process name";
        return -1;
    }
    name_len = length - PROTO_REQ_FIXED_SIZE;
    if (name_len > PROTO_MAX_NAME_LEN)
    {
        *why = "process name too long";
        return -1;
    }
    req->command = payload[0];
    req->signal = payload[1];
    if (req->command < CMD_FIRST || req->command > CMD_LAST)
    {
        *why = "unknown command code";
        return -1;
    }
    if (req->command == CMD_KILL)
    {
        if (req->signal == 0)
            req->signal = PROTO_SIG_DEFAULT;
        else if (proto_signal_name(req->signal) == NULL)
        {
            *why = "signal not allowed";
            return -1;
        }
    }
    else if (req->signal != 0)
    {
        *why = "signal given for a command that does not take one";
        return -1;
    }
    memcpy(req->name, payload + PROTO_REQ_FIXED_SIZE, name_len);
    req->name[name_len] = '\0';
    if (!proto_valid_name(req->name, name_len))
    {
        *why = "invalid process name";
        return -1;
    }
    return 0;
}

/*
 * Function    : proto_valid_name
 * Description : A process name is 1..PROTO_MAX_NAME_LEN characters of ASCII
 *               letters, digits and PROTO_NAME_EXTRA_CHARS, and does not
 *               start with '-' (so "kill -9 x" is not taken as a name)
 * Return      : 1 if valid, 0 otherwise
 */
int proto_valid_name(const char *name, size_t length)
{
    size_t i;

    if (length == 0 || length > PROTO_MAX_NAME_LEN || name[0] == '-')
        return 0;
    for (i = 0; i < length; i++)
    {
        char c = name[i];

        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            continue;
        if (c == '\0' || strchr(PROTO_NAME_EXTRA_CHARS, c) == NULL)
            return 0;
    }
    return 1;
}

/*
 * Function    : proto_command_from_name
 * Description : Maps "get-mem", "kill", ... to the CommandCode
 * Return      : The code, or -1 if the word is not a remote command
 */
int proto_command_from_name(const char *word)
{
    int cmd;

    for (cmd = CMD_FIRST; cmd <= CMD_LAST; cmd++)
    {
        if (strcmp(word, COMMAND_NAMES[cmd]) == 0)
            return cmd;
    }
    return -1;
}

/*
 * Function    : proto_command_name
 * Description : Maps a CommandCode to its name
 * Return      : The name, or "?" for an unknown code
 */
const char *proto_command_name(int command)
{
    if (command < CMD_FIRST || command > CMD_LAST)
        return "?";
    return COMMAND_NAMES[command];
}

/*
 * Function    : proto_signal_parse
 * Description : Parses the signal argument of "kill". Accepts a name with or
 *               without "SIG" (any case) or the number, optionally after a
 *               '-': TERM, SIGTERM, -TERM, 15, -15 ... Only the signals in
 *               SIGNALS[] are allowed.
 * Return      : 0 and *wire_signal on success, -1 if not allowed
 */
int proto_signal_parse(const char *text, int *wire_signal)
{
    size_t i;

    if (text[0] == '-')
        text++;
    if (text[0] == '\0' || strlen(text) >= SIGNAL_TEXT_MAX)
        return -1;
    if (text[0] >= '0' && text[0] <= '9')
    {
        for (i = 0; i < SIGNAL_COUNT; i++)
        {
            char number[SIGNAL_TEXT_MAX];

            snprintf(number, sizeof number, "%d", SIGNALS[i].wire);
            if (strcmp(text, number) == 0)
            {
                *wire_signal = SIGNALS[i].wire;
                return 0;
            }
        }
        return -1;
    }
    if (strncasecmp(text, SIGNAL_PREFIX, SIGNAL_PREFIX_LEN) == 0)
        text += SIGNAL_PREFIX_LEN;
    for (i = 0; i < SIGNAL_COUNT; i++)
    {
        if (strcasecmp(text, SIGNALS[i].name) == 0)
        {
            *wire_signal = SIGNALS[i].wire;
            return 0;
        }
    }
    return -1;
}

/*
 * Function    : proto_signal_name
 * Description : Name ("TERM", "KILL", ...) of an allowed wire signal number
 * Return      : The name, or NULL if the signal is not allowed
 */
const char *proto_signal_name(int wire_signal)
{
    size_t i;

    for (i = 0; i < SIGNAL_COUNT; i++)
    {
        if (SIGNALS[i].wire == wire_signal)
            return SIGNALS[i].name;
    }
    return NULL;
}

/*
 * Function    : proto_signal_native
 * Description : Converts an allowed wire signal number to this system's
 *               signal number
 * Return      : The signal number, or -1 if the signal is not allowed
 */
int proto_signal_native(int wire_signal)
{
    size_t i;

    for (i = 0; i < SIGNAL_COUNT; i++)
    {
        if (SIGNALS[i].wire == wire_signal)
            return SIGNALS[i].native;
    }
    return -1;
}

/*
 * Function    : proto_status_text
 * Description : Human readable text for a ReplyStatus
 */
const char *proto_status_text(int status)
{
    switch (status)
    {
    case STATUS_OK:        return "success";
    case STATUS_NOT_FOUND: return "no matching process";
    case STATUS_PARTIAL:   return "the request failed for some of the processes";
    case STATUS_FAILED:    return "the request failed";
    default:               return "unknown status";
    }
}
