/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : tftp.c
 *           Descriptions   : Functions to create/extract packets before/after sending/receiving
 *                            and the transfer engine (DATA sender and DATA receiver) used by
 *                            both the client and the server
 *           Objective      : Packet creation and extraction; lock-step file transfer with
 *                            timeouts, retransmission and transfer ID checks (RFC 1350)
 *           Usage          : tftp_session_init(), [tftp_send_request()], then
 *                            tftp_send_file() or tftp_receive_file()
 *           Output         : None (library module)
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* clock_gettime(), strcasecmp() */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <netinet/in.h>
#include "tftp.h"
#include "file.h"
#include "udp.h"

#define BYTE_BITS           8
#define BYTE_MASK           0xFF
#define PRINTABLE_FIRST     0x20    /* ' ' */
#define PRINTABLE_LAST      0x7E    /* '~' */
#define REPLACEMENT_CHAR    '?'
#define NS_PER_MS           1000000L
#define NS_PER_SECOND       1000000000L
#define DECIMAL_BASE        10

/* Results of wait_packet() */
#define WAIT_PACKET         0       /* A valid packet from the peer          */
#define WAIT_TIMEOUT        1       /* Deadline passed                       */
#define WAIT_CANCELLED      2       /* The cancel flag was set               */
#define WAIT_ERROR          3       /* Socket error (s->message is set)      */
#define WAIT_MALFORMED      4       /* A packet from the peer failed parsing */

#define MSG_CANCELLED       "Transfer cancelled by user"
#define MSG_TIMEOUT         "Transfer timed out (no answer from peer)"
#define MSG_MALFORMED       "Malformed TFTP packet"

/* RFC 1350 error messages, indexed by error code */
static const char *const error_strings[] =
{
    ESTRING_0, ESTRING_1, ESTRING_2, ESTRING_3, ESTRING_4, ESTRING_5, ESTRING_6, ESTRING_7
};

/* ------------------------------------------------------------------------- */
/*                         Packet formation and parsing                      */
/* ------------------------------------------------------------------------- */

/*
 * Function    : put_u16
 * Description : Stores a 16-bit value in network byte order
 */
static void put_u16(unsigned char *p, unsigned int value)
{
    p[0] = (unsigned char)((value >> BYTE_BITS) & BYTE_MASK);
    p[1] = (unsigned char)(value & BYTE_MASK);
}

/*
 * Function    : get_u16
 * Description : Reads a 16-bit value stored in network byte order
 */
static unsigned short get_u16(const unsigned char *p)
{
    return (unsigned short)(((unsigned int)p[0] << BYTE_BITS) | p[1]);
}

/*
 * Function    : copy_string_field
 * Description : Copies the NUL-terminated string that starts at 'p' (and must
 *               end before 'end') into 'out'
 * Return      : Pointer just past the terminating NUL, or NULL if the field
 *               has no NUL inside the packet or is empty
 */
static const unsigned char *copy_string_field(const unsigned char *p, const unsigned char *end,
                                              char *out, size_t out_size)
{
    const unsigned char *nul;
    size_t len;

    if (p >= end)
        return NULL;
    nul = memchr(p, '\0', (size_t)(end - p));
    if (nul == NULL)
        return NULL;
    len = (size_t)(nul - p);
    if (len == 0 || len >= out_size)
        return NULL;
    memcpy(out, p, len);
    out[len] = '\0';
    return nul + 1;
}

/*
 * Function    : packet_peek_opcode
 * Description : Reads the opcode of a raw packet without validating the rest
 * Return      : 0 on success, -1 if the packet is too short
 */
int packet_peek_opcode(const unsigned char *buf, size_t len, opcode_t *opcode)
{
    if (len < OPCODE_SIZE)
        return -1;
    *opcode = get_u16(buf);
    return 0;
}

/*
 * Function    : packet_parse
 * Description : Validates a received packet and extracts its fields. Nothing
 *               in the packet is trusted: lengths are checked against the
 *               datagram size, file name and mode must be non-empty and
 *               NUL-terminated inside the packet, ACKs must be exactly 4 bytes.
 *               Fields after the mode of a request (RFC 2347 options) are
 *               ignored, as RFC 2347 allows for servers without options.
 *               An ERROR message without its NUL is accepted up to the end of
 *               the packet; its non-printable bytes are replaced by '?'.
 * Return      : 0 on success, -1 if the packet is malformed or unknown
 */
int packet_parse(const unsigned char *buf, size_t len, packet_t *pkt)
{
    const unsigned char *end = buf + len;
    const unsigned char *p;

    if (len < OPCODE_SIZE || len > PACKETSIZE)
        return -1;
    pkt->opcode = get_u16(buf);

    switch (pkt->opcode)
    {
    case OPCODE_RRQ:
    case OPCODE_WRQ:
        p = copy_string_field(buf + OPCODE_SIZE, end, pkt->filename, sizeof pkt->filename);
        if (p == NULL)
            return -1;
        p = copy_string_field(p, end, pkt->mode, sizeof pkt->mode);
        if (p == NULL)
            return -1;
        return 0;

    case OPCODE_DATA:
        if (len < HEADER_SIZE)
            return -1;
        pkt->blocknum = get_u16(buf + OPCODE_SIZE);
        pkt->data_length = len - HEADER_SIZE;
        if (pkt->data_length > 0)
            memcpy(pkt->data, buf + HEADER_SIZE, pkt->data_length);
        return 0;

    case OPCODE_ACK:
        if (len != HEADER_SIZE)
            return -1;
        pkt->blocknum = get_u16(buf + OPCODE_SIZE);
        return 0;

    case OPCODE_ERROR:
    {
        size_t i, n;

        if (len < HEADER_SIZE)
            return -1;
        pkt->ecode = get_u16(buf + OPCODE_SIZE);
        n = len - HEADER_SIZE;
        p = memchr(buf + HEADER_SIZE, '\0', n);
        if (p != NULL)
            n = (size_t)(p - (buf + HEADER_SIZE));
        for (i = 0; i < n; i++)
        {
            unsigned char c = buf[HEADER_SIZE + i];

            pkt->estring[i] = (c >= PRINTABLE_FIRST && c <= PRINTABLE_LAST) ? (char)c
                                                                          : REPLACEMENT_CHAR;
        }
        pkt->estring[n] = '\0';
        return 0;
    }

    default:
        return -1;
    }
}

/*
 * Function    : form_request
 * Description : Builds an RRQ or WRQ: opcode, file name, 0, mode, 0
 * Return      : Packet length, or 0 if the name is empty or too long
 */
static size_t form_request(unsigned char *buf, opcode_t opcode, const char *filename,
                           const char *mode)
{
    size_t name_len = strlen(filename);
    size_t mode_len = strlen(mode);
    size_t total = OPCODE_SIZE + name_len + 1 + mode_len + 1;

    if (name_len == 0 || total > PACKETSIZE)
        return 0;
    put_u16(buf, opcode);
    memcpy(buf + OPCODE_SIZE, filename, name_len + 1);
    memcpy(buf + OPCODE_SIZE + name_len + 1, mode, mode_len + 1);
    return total;
}

/*
 * Function    : packet_form_rrq
 * Description : Builds a read request in buf (PACKETSIZE bytes)
 * Return      : Packet length, or 0 if it does not fit
 */
size_t packet_form_rrq(unsigned char *buf, const char *filename, const char *mode)
{
    return form_request(buf, OPCODE_RRQ, filename, mode);
}

/*
 * Function    : packet_form_wrq
 * Description : Builds a write request in buf (PACKETSIZE bytes)
 * Return      : Packet length, or 0 if it does not fit
 */
size_t packet_form_wrq(unsigned char *buf, const char *filename, const char *mode)
{
    return form_request(buf, OPCODE_WRQ, filename, mode);
}

/*
 * Function    : packet_form_data
 * Description : Builds a DATA packet: opcode, block number, 0-512 data bytes
 * Return      : Packet length, or 0 if len is larger than BLIMIT
 */
size_t packet_form_data(unsigned char *buf, bnum_t block, const unsigned char *data, size_t len)
{
    if (len > BLIMIT)
        return 0;
    put_u16(buf, OPCODE_DATA);
    put_u16(buf + OPCODE_SIZE, block);
    if (len > 0)
        memcpy(buf + HEADER_SIZE, data, len);
    return HEADER_SIZE + len;
}

/*
 * Function    : packet_form_ack
 * Description : Builds an ACK packet: opcode, block number
 * Return      : Packet length (4)
 */
size_t packet_form_ack(unsigned char *buf, bnum_t block)
{
    put_u16(buf, OPCODE_ACK);
    put_u16(buf + OPCODE_SIZE, block);
    return HEADER_SIZE;
}

/*
 * Function    : packet_form_error
 * Description : Builds an ERROR packet: opcode, error code, message, 0.
 *               A message that does not fit is shortened.
 * Return      : Packet length
 */
size_t packet_form_error(unsigned char *buf, ecode_t code, const char *message)
{
    size_t len = strlen(message);

    if (len > PACKETSIZE - HEADER_SIZE - 1)
        len = PACKETSIZE - HEADER_SIZE - 1;
    put_u16(buf, OPCODE_ERROR);
    put_u16(buf + OPCODE_SIZE, code);
    memcpy(buf + HEADER_SIZE, message, len);
    buf[HEADER_SIZE + len] = '\0';
    return HEADER_SIZE + len + 1;
}

/* ------------------------------------------------------------------------- */
/*                                  Helpers                                  */
/* ------------------------------------------------------------------------- */

/*
 * Function    : tftp_error_string
 * Description : RFC 1350 text of an error code
 */
const char *tftp_error_string(ecode_t code)
{
    if (code > ECODE_MAX)
        return "Unknown error code.";
    return error_strings[code];
}

/*
 * Function    : tftp_errno_to_ecode
 * Description : Chooses the TFTP error code that describes a local errno
 */
ecode_t tftp_errno_to_ecode(int err)
{
    switch (err)
    {
    case ENOENT:
    case ENOTDIR:
        return ECODE_1;
    case EACCES:
    case EPERM:
    case ELOOP:
    case EISDIR:
    case EROFS:
        return ECODE_2;
    case ENOSPC:
    case EDQUOT:
    case EFBIG:
        return ECODE_3;
    case EEXIST:
        return ECODE_6;
    default:
        return ECODE_0;
    }
}

/*
 * Function    : tftp_parse_mode
 * Description : Recognises a mode name, ignoring case ("NetASCII" is fine)
 */
tftp_mode_t tftp_parse_mode(const char *name)
{
    if (strcasecmp(name, MODE_NETASCII) == 0)
        return TFTP_MODE_NETASCII;
    if (strcasecmp(name, MODE_OCTET) == 0)
        return TFTP_MODE_OCTET;
    if (strcasecmp(name, MODE_MAIL) == 0)
        return TFTP_MODE_MAIL;
    return TFTP_MODE_INVALID;
}

/*
 * Function    : tftp_mode_name
 * Description : Wire name of a mode
 */
const char *tftp_mode_name(tftp_mode_t mode)
{
    switch (mode)
    {
    case TFTP_MODE_NETASCII:
        return MODE_NETASCII;
    case TFTP_MODE_OCTET:
        return MODE_OCTET;
    case TFTP_MODE_MAIL:
        return MODE_MAIL;
    default:
        return "invalid";
    }
}

/*
 * Function    : tftp_timeout_ms
 * Description : Retransmission timeout: TIMEOUT seconds, or the value of the
 *               TFTP_TIMEOUT_MS test hook if it is set to a valid number
 */
int tftp_timeout_ms(void)
{
    const char *text = getenv(TIMEOUT_ENV);
    char *end;
    long value;

    if (text == NULL || text[0] == '\0')
        return TIMEOUT * MS_PER_SECOND;
    errno = 0;
    value = strtol(text, &end, DECIMAL_BASE);
    if (errno != 0 || *end != '\0' || value < TIMEOUT_MS_MIN || value > TIMEOUT_MS_MAX)
        return TIMEOUT * MS_PER_SECOND;
    return (int)value;
}

/*
 * Function    : tftp_printable
 * Description : Copies a string for printing, replacing control characters
 *               and non-ASCII bytes by '?' (names and messages come from the
 *               network and must not send escape sequences to a terminal)
 */
void tftp_printable(const char *in, char *out, size_t size)
{
    size_t i;

    if (size == 0)
        return;
    for (i = 0; in[i] != '\0' && i + 1 < size; i++)
    {
        unsigned char c = (unsigned char)in[i];

        out[i] = (c >= PRINTABLE_FIRST && c <= PRINTABLE_LAST) ? (char)c : REPLACEMENT_CHAR;
    }
    out[i] = '\0';
}

/*
 * Function    : tftp_send_error
 * Description : Sends an ERROR packet. An empty or NULL message is replaced
 *               by the RFC text of the code. ERROR packets are never
 *               acknowledged or retransmitted.
 * Return      : 0 on success, -1 with errno set on error
 */
int tftp_send_error(int sock, const struct sockaddr_in *to, ecode_t code, const char *message)
{
    unsigned char buf[PACKETSIZE];
    size_t len;

    if (message == NULL || message[0] == '\0')
        message = tftp_error_string(code);
    len = packet_form_error(buf, code, message);
    return udp_send_packet(sock, buf, len, to);
}

/*
 * Function    : tftp_result_string
 * Description : Short description of a transfer result
 */
const char *tftp_result_string(int result)
{
    switch (result)
    {
    case TFTP_OK:
        return "success";
    case TFTP_ERR_TIMEOUT:
        return "timed out";
    case TFTP_ERR_REMOTE:
        return "error from peer";
    case TFTP_ERR_LOCAL:
        return "local error";
    case TFTP_ERR_PROTOCOL:
        return "protocol error";
    case TFTP_ERR_SOCKET:
        return "socket error";
    case TFTP_ERR_CANCELLED:
        return "cancelled";
    default:
        return "unknown result";
    }
}

/* ------------------------------------------------------------------------- */
/*                               Transfer engine                             */
/* ------------------------------------------------------------------------- */

/*
 * Function    : now_monotonic
 * Description : Current time of the monotonic clock (falls back to the
 *               wall clock in seconds if it is unavailable)
 */
static void now_monotonic(struct timespec *ts)
{
    if (clock_gettime(CLOCK_MONOTONIC, ts) != 0)
    {
        ts->tv_sec = time(NULL);
        ts->tv_nsec = 0;
    }
}

/*
 * Function    : deadline_set
 * Description : Sets *deadline to 'ms' milliseconds from now
 */
static void deadline_set(struct timespec *deadline, int ms)
{
    now_monotonic(deadline);
    deadline->tv_sec += ms / MS_PER_SECOND;
    deadline->tv_nsec += (long)(ms % MS_PER_SECOND) * NS_PER_MS;
    if (deadline->tv_nsec >= NS_PER_SECOND)
    {
        deadline->tv_sec++;
        deadline->tv_nsec -= NS_PER_SECOND;
    }
}

/*
 * Function    : deadline_remaining_ms
 * Description : Milliseconds left until the deadline (rounded up, 0 if past)
 */
static int deadline_remaining_ms(const struct timespec *deadline)
{
    struct timespec now;
    long long ns;

    now_monotonic(&now);
    ns = (long long)(deadline->tv_sec - now.tv_sec) * NS_PER_SECOND
         + (deadline->tv_nsec - now.tv_nsec);
    if (ns <= 0)
        return 0;
    return (int)((ns + NS_PER_MS - 1) / NS_PER_MS);
}

/*
 * Function    : tftp_session_init
 * Description : Prepares a session. 'locked' is 1 when the peer's TID is
 *               already known (server side: the client's address), 0 when
 *               the first answer will tell it (client side: the server
 *               answers from a new port).
 */
void tftp_session_init(tftp_session_t *s, int sock, const struct sockaddr_in *peer, int locked)
{
    memset(s, 0, sizeof *s);
    s->sock = sock;
    s->peer = *peer;
    s->peer_locked = locked;
    s->timeout_ms = tftp_timeout_ms();
    s->cancel = NULL;
    s->progress = NULL;
    s->progress_ctx = NULL;
    s->ecode = ECODE_0;
    s->message[0] = '\0';
}

/*
 * Function    : set_message
 * Description : Records the reason of a failure for the caller
 */
static void set_message(tftp_session_t *s, const char *what, const char *detail)
{
    if (detail != NULL)
        snprintf(s->message, sizeof s->message, "%s: %s", what, detail);
    else
        snprintf(s->message, sizeof s->message, "%s", what);
}

/*
 * Function    : send_last
 * Description : (Re)sends the packet kept for retransmission. Temporary
 *               shortage of buffers counts as a lost packet; the timeout
 *               will send it again.
 * Return      : 0 on success, -1 on a socket error (s->message is set)
 */
static int send_last(tftp_session_t *s)
{
    if (udp_send_packet(s->sock, s->last, s->last_len, &s->peer) == 0)
        return 0;
    if (errno == ENOBUFS || errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOMEM)
        return 0;
    set_message(s, "Cannot send packet", strerror(errno));
    return -1;
}

/*
 * Function    : reply_unknown_tid
 * Description : Answers a packet from a foreign address/port with error 5,
 *               without disturbing the transfer (RFC 1350 section 4). ERROR
 *               packets are not answered, so two programs can never bounce
 *               errors back and forth.
 */
static void reply_unknown_tid(tftp_session_t *s, const unsigned char *buf, size_t len,
                              const struct sockaddr_in *from)
{
    opcode_t opcode;

    if (packet_peek_opcode(buf, len, &opcode) == 0 && IS_ERROR(opcode))
        return;
    /* Best effort: if this fails there is nothing more to do for a stranger */
    (void)tftp_send_error(s->sock, from, ECODE_5, ESTRING_5);
}

/*
 * Function    : wait_packet
 * Description : Waits until the deadline for the next packet of this
 *               transfer. Packets from other transfer IDs are answered with
 *               error 5 and skipped. Before the peer is locked, any port of
 *               the peer's host is accepted (the server answers from a new
 *               port); the caller decides whether to lock onto it.
 *               Packets that are ignored do not extend the deadline.
 * Return      : WAIT_PACKET, WAIT_MALFORMED, WAIT_TIMEOUT, WAIT_CANCELLED or
 *               WAIT_ERROR
 */
static int wait_packet(tftp_session_t *s, const struct timespec *deadline, packet_t *pkt,
                       struct sockaddr_in *from)
{
    unsigned char buf[RECV_BUFFER];

    for (;;)
    {
        ssize_t n;
        int remaining, known;

        if (s->cancel != NULL && *s->cancel)
            return WAIT_CANCELLED;
        remaining = deadline_remaining_ms(deadline);
        if (remaining == 0)
            return WAIT_TIMEOUT;

        n = udp_receive_packet(s->sock, buf, sizeof buf, from, remaining);
        if (n == UDP_TIMEOUT || n == UDP_INTERRUPTED)
            continue;
        if (n < 0)
        {
            set_message(s, "Cannot receive packet", strerror(errno));
            return WAIT_ERROR;
        }

        known = s->peer_locked ? udp_same_endpoint(from, &s->peer)
                               : udp_same_host(from, &s->peer);
        if (!known)
        {
            reply_unknown_tid(s, buf, (size_t)n, from);
            continue;
        }
        if (packet_parse(buf, (size_t)n, pkt) != 0)
            return WAIT_MALFORMED;
        return WAIT_PACKET;
    }
}

/*
 * Function    : retransmit
 * Description : Handles a timeout: sends the last packet again unless the
 *               retry limit is reached
 * Return      : 0 if the packet was sent again, TFTP_ERR_TIMEOUT or
 *               TFTP_ERR_SOCKET if the transfer has to stop
 */
static int retransmit(tftp_session_t *s, int *retries, struct timespec *deadline)
{
    if (++*retries > TIMEOUT_LIMIT)
    {
        set_message(s, MSG_TIMEOUT, NULL);
        return TFTP_ERR_TIMEOUT;
    }
    if (send_last(s) != 0)
        return TFTP_ERR_SOCKET;
    s->retransmissions++;
    deadline_set(deadline, s->timeout_ms);
    return 0;
}

/*
 * Function    : fail_local
 * Description : Ends a transfer because of a local file error: tells the
 *               peer with the matching error code and records the reason
 * Return      : TFTP_ERR_LOCAL
 */
static int fail_local(tftp_session_t *s, int err, const char *what)
{
    ecode_t code = tftp_errno_to_ecode(err);
    const char *text = strerror(err);

    s->ecode = code;
    if (s->peer_locked)     /* Best effort: ERROR packets are not acknowledged */
        (void)tftp_send_error(s->sock, &s->peer, code,
                              (code == ECODE_0) ? text : tftp_error_string(code));
    set_message(s, what, text);
    return TFTP_ERR_LOCAL;
}

/*
 * Function    : fail_protocol
 * Description : Ends a transfer because the peer broke the protocol:
 *               sends error 4 and records the reason
 * Return      : TFTP_ERR_PROTOCOL
 */
static int fail_protocol(tftp_session_t *s, const char *why)
{
    s->ecode = ECODE_4;
    (void)tftp_send_error(s->sock, &s->peer, ECODE_4, why);     /* Best effort */
    set_message(s, why, NULL);
    return TFTP_ERR_PROTOCOL;
}

/*
 * Function    : fail_remote
 * Description : Ends a transfer because the peer sent an ERROR packet
 * Return      : TFTP_ERR_REMOTE
 */
static int fail_remote(tftp_session_t *s, const packet_t *pkt)
{
    s->ecode = pkt->ecode;
    set_message(s, (pkt->estring[0] != '\0') ? pkt->estring : tftp_error_string(pkt->ecode), NULL);
    return TFTP_ERR_REMOTE;
}

/*
 * Function    : fail_wait
 * Description : Ends a transfer after wait_packet() returned
 *               WAIT_CANCELLED or WAIT_ERROR
 * Return      : TFTP_ERR_CANCELLED or TFTP_ERR_SOCKET
 */
static int fail_wait(tftp_session_t *s, int wait_result)
{
    if (wait_result == WAIT_CANCELLED)
    {
        if (s->peer_locked)     /* Best effort */
            (void)tftp_send_error(s->sock, &s->peer, ECODE_0, MSG_CANCELLED);
        s->ecode = ECODE_0;
        set_message(s, MSG_CANCELLED, NULL);
        return TFTP_ERR_CANCELLED;
    }
    return TFTP_ERR_SOCKET;
}

/*
 * Function    : lock_peer
 * Description : Fixes the peer's TID to the sender of its first valid answer
 */
static void lock_peer(tftp_session_t *s, const struct sockaddr_in *from)
{
    s->peer = *from;
    s->peer_locked = 1;
}

/*
 * Function    : report_progress
 * Description : Calls the progress callback, if any
 */
static void report_progress(tftp_session_t *s)
{
    if (s->progress != NULL)
        s->progress(s->progress_ctx, s->blocks);
}

/*
 * Function    : tftp_send_request
 * Description : Client side: sends an RRQ or WRQ to the server's well-known
 *               port and keeps it for retransmission until the first answer
 * Return      : TFTP_OK, TFTP_ERR_LOCAL (name too long) or TFTP_ERR_SOCKET
 */
int tftp_send_request(tftp_session_t *s, opcode_t opcode, const char *filename, tftp_mode_t mode)
{
    const char *mode_name = tftp_mode_name(mode);

    s->last_len = IS_RRQ(opcode) ? packet_form_rrq(s->last, filename, mode_name)
                                 : packet_form_wrq(s->last, filename, mode_name);
    if (s->last_len == 0)
    {
        set_message(s, "File name is empty or too long", NULL);
        return TFTP_ERR_LOCAL;
    }
    s->request_pending = 1;
    if (send_last(s) != 0)
        return TFTP_ERR_SOCKET;
    return TFTP_OK;
}

/*
 * Function    : tftp_send_file
 * Description : DATA sender (server after an RRQ, client after a WRQ).
 *               If a WRQ is pending, first waits for its ACK 0; otherwise
 *               starts with DATA block 1. Each block is sent when the
 *               previous one is acknowledged (lock step). A block shorter
 *               than 512 bytes ends the transfer, so a file whose size is a
 *               multiple of 512 ends with an empty block.
 *               On timeout the last DATA is sent again. A duplicate ACK is
 *               ignored and never causes a retransmission, which avoids the
 *               Sorcerer's Apprentice bug (RFC 1123, 4.2.3.1). Block numbers
 *               wrap from 65535 to 0, so files of any size can be sent.
 * Return      : TFTP_OK or one of the TFTP_ERR_* codes (s->message is set)
 */
int tftp_send_file(tftp_session_t *s, tftp_file_t *f)
{
    unsigned char data[BLIMIT];
    packet_t pkt;
    struct sockaddr_in from;
    struct timespec deadline;
    unsigned long long sent = 0;    /* DATA blocks sent; ACK of block 'sent' awaited */
    size_t last_size = BLIMIT;      /* Payload size of the last DATA sent            */
    int retries = 0;
    int need_next = !s->request_pending;

    deadline_set(&deadline, s->timeout_ms);
    for (;;)
    {
        int rc;

        if (need_next)
        {
            ssize_t n;

            if (sent > 0 && last_size < BLIMIT)
                return TFTP_OK;                 /* Final block acknowledged */

            n = file_read_block(f, data, sizeof data);
            if (n < 0)
                return fail_local(s, errno, "Cannot read file");
            sent++;
            last_size = (size_t)n;
            s->last_len = packet_form_data(s->last, (bnum_t)sent, data, last_size);
            s->request_pending = 0;
            if (send_last(s) != 0)
                return TFTP_ERR_SOCKET;
            retries = 0;
            need_next = 0;
            deadline_set(&deadline, s->timeout_ms);
        }

        rc = wait_packet(s, &deadline, &pkt, &from);
        if (rc == WAIT_TIMEOUT)
        {
            rc = retransmit(s, &retries, &deadline);
            if (rc != 0)
                return rc;
            continue;
        }
        if (rc == WAIT_CANCELLED || rc == WAIT_ERROR)
            return fail_wait(s, rc);
        if (rc == WAIT_MALFORMED)
        {
            if (!s->peer_locked)
                continue;
            return fail_protocol(s, MSG_MALFORMED);
        }

        if (!s->peer_locked)
        {
            /* First answer to the WRQ: only ACK 0 or ERROR may fix the TID */
            if (!(IS_ACK(pkt.opcode) && pkt.blocknum == 0) && !IS_ERROR(pkt.opcode))
                continue;
            lock_peer(s, &from);
        }
        if (IS_ERROR(pkt.opcode))
            return fail_remote(s, &pkt);
        if (!IS_ACK(pkt.opcode))
            return fail_protocol(s, ESTRING_4);
        if (pkt.blocknum != (bnum_t)sent)
            continue;                           /* Duplicate or stale ACK: ignore */

        if (sent > 0)
        {
            s->blocks = sent;
            s->bytes += last_size;
            report_progress(s);
        }
        need_next = 1;
    }
}

/*
 * Function    : dally
 * Description : After the final ACK, waits a little in case that ACK is
 *               lost: the sender would then repeat the final DATA, which is
 *               answered with the final ACK again (RFC 1350 section 6)
 */
static void dally(tftp_session_t *s, bnum_t final_block)
{
    packet_t pkt;
    struct sockaddr_in from;
    struct timespec deadline;
    int repeats = 0;

    deadline_set(&deadline, s->timeout_ms * DALLY_FACTOR);
    while (repeats < TIMEOUT_LIMIT)
    {
        int rc = wait_packet(s, &deadline, &pkt, &from);

        if (rc == WAIT_MALFORMED)
            continue;
        if (rc != WAIT_PACKET)
            return;
        if (IS_DATA(pkt.opcode) && pkt.blocknum == final_block)
        {
            if (send_last(s) != 0)
                return;
            repeats++;
            deadline_set(&deadline, s->timeout_ms * DALLY_FACTOR);
        }
    }
}

/*
 * Function    : tftp_receive_file
 * Description : DATA receiver (server after a WRQ, client after an RRQ).
 *               If an RRQ is pending, waits for DATA 1; otherwise starts by
 *               sending ACK 0. Every new block is written and acknowledged;
 *               a repeated block (our ACK was lost) is acknowledged again.
 *               On timeout the last ACK (or the RRQ) is sent again. The
 *               final block (< 512 bytes) is written and the file closed
 *               before it is acknowledged, so an acknowledged file is complete.
 * Return      : TFTP_OK or one of the TFTP_ERR_* codes (s->message is set)
 */
int tftp_receive_file(tftp_session_t *s, tftp_file_t *f)
{
    packet_t pkt;
    struct sockaddr_in from;
    struct timespec deadline;
    unsigned long long received = 0;    /* Blocks written; next expected: received + 1 */
    int retries = 0;

    if (!s->request_pending)
    {
        s->last_len = packet_form_ack(s->last, 0);
        if (send_last(s) != 0)
            return TFTP_ERR_SOCKET;
    }
    deadline_set(&deadline, s->timeout_ms);

    for (;;)
    {
        int rc = wait_packet(s, &deadline, &pkt, &from);

        if (rc == WAIT_TIMEOUT)
        {
            rc = retransmit(s, &retries, &deadline);
            if (rc != 0)
                return rc;
            continue;
        }
        if (rc == WAIT_CANCELLED || rc == WAIT_ERROR)
            return fail_wait(s, rc);
        if (rc == WAIT_MALFORMED)
        {
            if (!s->peer_locked)
                continue;
            return fail_protocol(s, MSG_MALFORMED);
        }

        if (!s->peer_locked)
        {
            /* First answer to the RRQ: only DATA 1 or ERROR may fix the TID */
            if (!(IS_DATA(pkt.opcode) && pkt.blocknum == 1) && !IS_ERROR(pkt.opcode))
                continue;
            lock_peer(s, &from);
        }
        if (IS_ERROR(pkt.opcode))
            return fail_remote(s, &pkt);
        if (!IS_DATA(pkt.opcode))
            return fail_protocol(s, ESTRING_4);

        if (pkt.blocknum == (bnum_t)(received + 1))
        {
            int final = pkt.data_length < BLIMIT;

            if (file_write_block(f, pkt.data, pkt.data_length) != 0)
                return fail_local(s, errno, "Cannot write file");
            if (final && file_finish_write(f) != 0)
                return fail_local(s, errno, "Cannot write file");

            received++;
            s->blocks = received;
            s->bytes += pkt.data_length;
            s->request_pending = 0;
            s->last_len = packet_form_ack(s->last, pkt.blocknum);
            if (send_last(s) != 0)
                return TFTP_ERR_SOCKET;
            report_progress(s);

            if (final)
            {
                if (s->dally)
                    dally(s, pkt.blocknum);
                return TFTP_OK;
            }
            retries = 0;
            deadline_set(&deadline, s->timeout_ms);
        }
        else if (received > 0 && pkt.blocknum == (bnum_t)received)
        {
            /* The sender did not get our ACK and repeated the block */
            if (send_last(s) != 0)
                return TFTP_ERR_SOCKET;
        }
        /* Any other block number is stale: ignore it */
    }
}
