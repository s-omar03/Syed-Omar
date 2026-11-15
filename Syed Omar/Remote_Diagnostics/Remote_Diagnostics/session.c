/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : session.c
 *           Descriptions   : Server side of one client connection and the server's activity log
 *           Objective      : Runs in the child process created for each client:
 *                            greet -> loop { read request -> validate -> ACK/NACK ->
 *                            run command -> send reply text in DATA frames -> END(status) }
 *           Usage          : Linked into ./server
 *           Output         : Frames to the client, activity lines on stdout
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include "diag.h"
#include "ports.h"
#include "procinfo.h"
#include "protocol.h"
#include "session.h"
#include "strbuf.h"
#include "util.h"

#define TRUNCATION_NOTE  "\n... output truncated (reply size limit reached)\n"

/*
 * Function    : server_log
 * Description : Writes one activity line "RMS Server: ..." to stdout with a
 *               single write(), so lines from several child processes never
 *               get mixed up
 */
void server_log(const char *fmt, ...)
{
    char line[LOG_LINE_MAX];
    size_t prefix_len = strlen(LOG_PREFIX), len;
    va_list ap;
    int n;

    memcpy(line, LOG_PREFIX, prefix_len);
    va_start(ap, fmt);
    n = vsnprintf(line + prefix_len, sizeof line - prefix_len - 1, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    len = prefix_len + (size_t)n;
    if (len > sizeof line - 2)
        len = sizeof line - 2;          /* cut long lines, keep room for '\n' */
    line[len++] = '\n';
    (void)util_write_all(STDOUT_FILENO, line, len);
}

/*
 * Function    : send_nack
 * Description : Sends a NACK frame with a reason text
 * Return      : PROTO_OK or an error code
 */
static int send_nack(int fd, const char *reason)
{
    return proto_send(fd, MSG_NACK, reason, strlen(reason));
}

/*
 * Function    : close_with_error
 * Description : Ends a connection after a protocol error: tells the client
 *               why, stops sending, and reads what the client still sends
 *               for a moment so that close() does not reset the connection
 *               before the client has read the reason
 */
static void close_with_error(int fd, const char *peer, const char *reason)
{
    server_log("[%s] Protocol error: %s - closing the connection", peer, reason);
    (void)send_nack(fd, reason);
    (void)shutdown(fd, SHUT_WR);
    proto_drain(fd, SESSION_DRAIN_TIMEOUT_MS, SESSION_DRAIN_MAX_BYTES);
}

/*
 * Function    : run_command
 * Description : Finds the processes named in the request and runs the
 *               command on them, writing the reply text to 'out'
 * Return      : ReplyStatus
 */
static int run_command(const Request *req, StrBuf *out)
{
    ProcList list;
    int status;

    if (proc_find_by_name(req->name, &list) != 0)
    {
        sb_printf(out, "Could not read the process list: %s\n", strerror(errno));
        return STATUS_FAILED;
    }
    if (list.count == 0)
    {
        sb_printf(out, "No process named '%s' is running\n", req->name);
        proc_list_free(&list);
        return STATUS_NOT_FOUND;
    }
    switch (req->command)
    {
    case CMD_GET_MEM:   status = diag_get_mem(&list, out);      break;
    case CMD_GET_CPU:   status = diag_get_cpu(&list, out);      break;
    case CMD_GET_PORTS: status = ports_report(&list, out);      break;
    case CMD_GET_FD:    status = diag_get_open_fd(&list, out);  break;
    default:            status = diag_kill(&list, req->signal, req->name, out); break;
    }
    if (list.truncated)
        sb_printf(out, "Note: only the first %d processes named '%s' were handled\n",
                  PROC_MAX_MATCHES, req->name);
    proc_list_free(&list);
    return status;
}

/*
 * Function    : send_reply
 * Description : Sends the reply text in DATA frames of at most
 *               PROTO_MAX_PAYLOAD bytes, then END with the status byte.
 *               The final newline is not sent (the client closes the
 *               quoted RESPONSE right after the text).
 * Return      : PROTO_OK or an error code
 */
static int send_reply(int fd, const StrBuf *reply, int status)
{
    const char *text = sb_text(reply);
    size_t length = reply->length, offset = 0;
    unsigned char status_byte = (unsigned char)status;
    int rc;

    if (length > 0 && text[length - 1] == '\n')
        length--;
    while (offset < length)
    {
        size_t chunk = length - offset;

        if (chunk > PROTO_MAX_PAYLOAD)
            chunk = PROTO_MAX_PAYLOAD;
        rc = proto_send(fd, MSG_DATA, text + offset, chunk);
        if (rc != PROTO_OK)
            return rc;
        offset += chunk;
    }
    return proto_send(fd, MSG_END, &status_byte, sizeof status_byte);
}

/*
 * Function    : handle_request
 * Description : Validates one request frame. An invalid request gets a NACK
 *               and the connection stays open; a valid one gets an ACK, is
 *               executed and answered.
 * Return      : 0 to keep serving, -1 if the connection failed
 */
static int handle_request(int fd, const char *peer, const Frame *frame)
{
    const char *why = "invalid request";
    StrBuf reply;
    Request req;
    int status, rc;

    if (proto_decode_request(frame->payload, frame->length, &req, &why) != 0)
    {
        server_log("[%s] Rejected request: %s", peer, why);
        return send_nack(fd, why) == PROTO_OK ? 0 : -1;
    }
    if (req.command == CMD_KILL)
        server_log("[%s] Request: kill %s (SIG%s)", peer, req.name, proto_signal_name(req.signal));
    else
        server_log("[%s] Request: %s %s", peer, proto_command_name(req.command), req.name);

    if (proto_send(fd, MSG_ACK, NULL, 0) != PROTO_OK)
        return -1;
    sb_init(&reply, REPLY_MAX_SIZE);
    status = run_command(&req, &reply);
    if (reply.truncated)
        sb_append_note(&reply, TRUNCATION_NOTE);
    rc = send_reply(fd, &reply, status);
    sb_free(&reply);
    if (rc != PROTO_OK)
    {
        server_log("[%s] Could not send the reply: %s", peer, proto_result_text(rc));
        return -1;
    }
    return 0;
}

/*
 * Function    : session_run
 * Description : Serves one client until it disconnects, sends something
 *               that breaks the protocol, or stays idle too long
 */
void session_run(int fd, const char *peer)
{
    Frame frame;

    if (proto_send(fd, MSG_HELLO, NULL, 0) != PROTO_OK)
    {
        server_log("[%s] Could not greet the client", peer);
        return;
    }
    for (;;)
    {
        int rc = proto_recv(fd, &frame, SESSION_IDLE_TIMEOUT_MS);

        if (rc == PROTO_OK)
        {
            if (frame.type != MSG_REQUEST)
            {
                close_with_error(fd, peer, "unexpected message type");
                return;
            }
            if (handle_request(fd, peer, &frame) != 0)
                return;
            continue;
        }
        switch (rc)
        {
        case PROTO_ERR_CLOSED:
            server_log("[%s] Client disconnected", peer);
            return;
        case PROTO_ERR_TIMEOUT:
            close_with_error(fd, peer, "timed out waiting for the client");
            return;
        case PROTO_ERR_MAGIC:
        case PROTO_ERR_VERSION:
        case PROTO_ERR_TYPE:
        case PROTO_ERR_LENGTH:
            close_with_error(fd, peer, proto_result_text(rc));
            return;
        default:
            server_log("[%s] Connection lost: %s", peer, proto_result_text(rc));
            return;
        }
    }
}
