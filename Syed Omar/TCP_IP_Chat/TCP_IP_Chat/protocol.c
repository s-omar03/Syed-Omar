/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : protocol.c
 *           Descriptions   : Encoding, decoding and validation of the chat packets, and the
 *                            send / receive loops that cope with partial transfers and EINTR
 *           Objective      : Move whole packets over a TCP stream safely: every integer in
 *                            network byte order, every length and string checked on arrival
 *           Usage          : Used by server.c, session.c, chat_room.c and client.c through
 *                            protocol.h
 *           Output         : None (library module)
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* strnlen() */

#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "protocol.h"

/*
 * Wire format
 * -----------
 * Every packet is an 8-byte header followed by a body:
 *
 *     uint32 type      PKT_* value          network byte order
 *     uint32 length    body size in bytes   network byte order
 *
 * Strings are fixed-size fields padded with NUL bytes. A field must contain
 * at least one NUL byte, otherwise the packet is rejected as malformed.
 * The receiver checks the exact body length of every packet type before it
 * reads the body, so a hostile length can never make it read or allocate
 * more than PROTO_MAX_BODY_SIZE bytes.
 */

#define PROTO_PACKET_MAX_SIZE   (PROTO_HEADER_SIZE + PROTO_MAX_BODY_SIZE)

/*
 * Function    : put_u32
 * Description : Stores a 32-bit value in network byte order
 * Return      : Pointer just past the stored value
 */
static unsigned char *put_u32(unsigned char *p, uint32_t value)
{
    uint32_t net = htonl(value);

    memcpy(p, &net, sizeof net);
    return p + PROTO_U32_SIZE;
}

/*
 * Function    : get_u32
 * Description : Reads a 32-bit value stored in network byte order
 */
static uint32_t get_u32(const unsigned char *p)
{
    uint32_t net;

    memcpy(&net, p, sizeof net);
    return ntohl(net);
}

/*
 * Function    : put_text
 * Description : Stores a string in a fixed-size field. The string is cut to
 *               size - 1 characters and the rest of the field is zeroed, so
 *               no stale memory is ever sent.
 * Return      : Pointer just past the field
 */
static unsigned char *put_text(unsigned char *p, const char *text, size_t size)
{
    size_t len = strnlen(text, size - 1);

    memcpy(p, text, len);
    memset(p + len, 0, size - len);
    return p + size;
}

/*
 * Function    : get_text
 * Description : Copies a fixed-size field into a string. The field must
 *               contain a NUL byte; everything after it is zeroed.
 * Return      : 0 on success, -1 when the field is not NUL-terminated
 */
static int get_text(char *dst, const unsigned char *p, size_t size)
{
    const unsigned char *nul = memchr(p, '\0', size);
    size_t len;

    if (nul == NULL)
        return -1;
    len = (size_t)(nul - p);
    memcpy(dst, p, len);
    memset(dst + len, 0, size - len);
    return 0;
}

/*
 * Function    : send_all
 * Description : Sends the whole buffer, continuing after partial sends and
 *               EINTR. MSG_NOSIGNAL turns a closed peer into an EPIPE error
 *               instead of a SIGPIPE signal.
 * Return      : PROTO_OK or PROTO_ERR_IO (also on a send timeout)
 */
static int send_all(int fd, const unsigned char *buf, size_t len)
{
    while (len > 0)
    {
        ssize_t sent = send(fd, buf, len, MSG_NOSIGNAL);

        if (sent < 0)
        {
            if (errno == EINTR)
                continue;
            return PROTO_ERR_IO;
        }
        if (sent == 0)
            return PROTO_ERR_IO;
        buf += sent;
        len -= (size_t)sent;
    }
    return PROTO_OK;
}

/*
 * Function    : recv_all
 * Description : Receives exactly len bytes, continuing after partial reads
 *               and EINTR
 * Return      : PROTO_OK; PROTO_CLOSED when the peer closed the connection
 *               before the first byte and 'first' is set; PROTO_ERR_IO on
 *               errors or when the connection ends in the middle of a packet
 */
static int recv_all(int fd, unsigned char *buf, size_t len, int first)
{
    size_t got = 0;

    while (got < len)
    {
        ssize_t n = recv(fd, buf + got, len - got, 0);

        if (n == 0)
            return (got == 0 && first) ? PROTO_CLOSED : PROTO_ERR_IO;
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return PROTO_ERR_IO;
        }
        got += (size_t)n;
    }
    return PROTO_OK;
}

/*
 * Function    : send_packet
 * Description : Prepends the header to a body and sends both in one call
 * Return      : PROTO_OK or PROTO_ERR_IO
 */
static int send_packet(int fd, uint32_t type, const unsigned char *body, size_t len)
{
    unsigned char packet[PROTO_PACKET_MAX_SIZE];
    unsigned char *p = packet;

    if (len > PROTO_MAX_BODY_SIZE)
        return PROTO_ERR_IO;
    p = put_u32(p, type);
    p = put_u32(p, (uint32_t)len);
    if (len > 0)
        memcpy(p, body, len);
    return send_all(fd, packet, PROTO_HEADER_SIZE + len);
}

/*
 * Function    : proto_send_auth
 * Description : Sends a register / login request (spec 3.1)
 * Return      : PROTO_OK or PROTO_ERR_IO
 */
int proto_send_auth(int fd, const char *username, const char *password, uint32_t option)
{
    unsigned char body[PROTO_AUTH_BODY_SIZE];
    unsigned char *p = body;

    p = put_text(p, username, PROTO_NAME_SIZE);
    p = put_text(p, password, PROTO_PASS_SIZE);
    put_u32(p, option);
    return send_packet(fd, PKT_AUTH_REQUEST, body, sizeof body);
}

/*
 * Function    : proto_send_reply
 * Description : Sends an error / success message (spec 3.2 and 3.6)
 * Return      : PROTO_OK or PROTO_ERR_IO
 */
int proto_send_reply(int fd, uint32_t code, const char *text)
{
    unsigned char body[PROTO_REPLY_BODY_SIZE];
    unsigned char *p = body;

    p = put_u32(p, code);
    put_text(p, text, PROTO_REPLY_TEXT_SIZE);
    return send_packet(fd, PKT_SERVER_REPLY, body, sizeof body);
}

/*
 * Function    : proto_send_chat
 * Description : Sends a chat message or a logout request (spec 3.3 - 3.5)
 * Return      : PROTO_OK or PROTO_ERR_IO
 */
int proto_send_chat(int fd, const char *sender, const char *receiver,
                    const char *text, uint32_t chat_option)
{
    unsigned char body[PROTO_CHAT_BODY_SIZE];
    unsigned char *p = body;

    p = put_text(p, sender, PROTO_NAME_SIZE);
    p = put_text(p, receiver, PROTO_NAME_SIZE);
    p = put_text(p, text, PROTO_TEXT_SIZE);
    put_u32(p, chat_option);
    return send_packet(fd, PKT_CHAT, body, sizeof body);
}

/*
 * Function    : proto_send_status
 * Description : Sends a "user is ONLINE / OFFLINE" notice
 * Return      : PROTO_OK or PROTO_ERR_IO
 */
int proto_send_status(int fd, const char *username, uint32_t status)
{
    unsigned char body[PROTO_STATUS_BODY_SIZE];
    unsigned char *p = body;

    p = put_text(p, username, PROTO_NAME_SIZE);
    put_u32(p, status);
    return send_packet(fd, PKT_USER_STATUS, body, sizeof body);
}

/*
 * Function    : proto_send_list_request
 * Description : Asks the server for the list of online users
 * Return      : PROTO_OK or PROTO_ERR_IO
 */
int proto_send_list_request(int fd)
{
    return send_packet(fd, PKT_LIST_REQUEST, NULL, 0);
}

/*
 * Function    : proto_send_user_list
 * Description : Sends a list of user names (at most PROTO_MAX_LIST_USERS)
 * Return      : PROTO_OK or PROTO_ERR_IO
 */
int proto_send_user_list(int fd, char names[][PROTO_NAME_SIZE], uint32_t count)
{
    unsigned char body[PROTO_LIST_BODY_MAX];
    unsigned char *p = body;
    uint32_t i;

    if (count > PROTO_MAX_LIST_USERS)
        count = PROTO_MAX_LIST_USERS;
    p = put_u32(p, count);
    for (i = 0; i < count; i++)
        p = put_text(p, names[i], PROTO_NAME_SIZE);
    return send_packet(fd, PKT_USER_LIST, body, (size_t)(p - body));
}

/*
 * Function    : body_length_ok
 * Description : Checks the body length announced in a header against the
 *               layout of the packet type
 * Return      : 1 when the type is known and the length is valid, else 0
 */
static int body_length_ok(uint32_t type, uint32_t length)
{
    switch (type)
    {
    case PKT_AUTH_REQUEST:
        return length == PROTO_AUTH_BODY_SIZE;
    case PKT_SERVER_REPLY:
        return length == PROTO_REPLY_BODY_SIZE;
    case PKT_CHAT:
        return length == PROTO_CHAT_BODY_SIZE;
    case PKT_USER_STATUS:
        return length == PROTO_STATUS_BODY_SIZE;
    case PKT_LIST_REQUEST:
        return length == 0;
    case PKT_USER_LIST:
        return length >= PROTO_U32_SIZE && length <= PROTO_LIST_BODY_MAX
               && (length - PROTO_U32_SIZE) % PROTO_NAME_SIZE == 0;
    default:
        return 0;
    }
}

/*
 * Function    : parse_body
 * Description : Decodes a body whose length has already been checked
 * Return      : PROTO_OK or PROTO_ERR_BAD
 */
static int parse_body(Packet *pkt, const unsigned char *body, uint32_t length)
{
    const unsigned char *p = body;
    uint32_t i;

    switch (pkt->type)
    {
    case PKT_AUTH_REQUEST:
        if (get_text(pkt->body.auth.username, p, PROTO_NAME_SIZE) != 0
            || get_text(pkt->body.auth.password, p + PROTO_NAME_SIZE, PROTO_PASS_SIZE) != 0)
            return PROTO_ERR_BAD;
        pkt->body.auth.option = get_u32(p + PROTO_NAME_SIZE + PROTO_PASS_SIZE);
        return PROTO_OK;

    case PKT_SERVER_REPLY:
        pkt->body.reply.code = get_u32(p);
        if (pkt->body.reply.code > REPLY_FATAL
            || get_text(pkt->body.reply.message, p + PROTO_U32_SIZE, PROTO_REPLY_TEXT_SIZE) != 0)
            return PROTO_ERR_BAD;
        return PROTO_OK;

    case PKT_CHAT:
        if (get_text(pkt->body.chat.sender, p, PROTO_NAME_SIZE) != 0
            || get_text(pkt->body.chat.receiver, p + PROTO_NAME_SIZE, PROTO_NAME_SIZE) != 0
            || get_text(pkt->body.chat.message, p + 2 * PROTO_NAME_SIZE, PROTO_TEXT_SIZE) != 0)
            return PROTO_ERR_BAD;
        pkt->body.chat.chat_option = get_u32(p + 2 * PROTO_NAME_SIZE + PROTO_TEXT_SIZE);
        return PROTO_OK;

    case PKT_USER_STATUS:
        if (get_text(pkt->body.status.username, p, PROTO_NAME_SIZE) != 0)
            return PROTO_ERR_BAD;
        pkt->body.status.status = get_u32(p + PROTO_NAME_SIZE);
        if (pkt->body.status.status != STATUS_ONLINE && pkt->body.status.status != STATUS_OFFLINE)
            return PROTO_ERR_BAD;
        return PROTO_OK;

    case PKT_LIST_REQUEST:
        return PROTO_OK;

    case PKT_USER_LIST:
        pkt->body.list.total = get_u32(p);
        if (pkt->body.list.total != (length - PROTO_U32_SIZE) / PROTO_NAME_SIZE)
            return PROTO_ERR_BAD;
        p += PROTO_U32_SIZE;
        for (i = 0; i < pkt->body.list.total; i++, p += PROTO_NAME_SIZE)
        {
            if (get_text(pkt->body.list.names[i], p, PROTO_NAME_SIZE) != 0)
                return PROTO_ERR_BAD;
        }
        return PROTO_OK;

    default:
        return PROTO_ERR_BAD;
    }
}

/*
 * Function    : proto_recv_packet
 * Description : Receives and validates one whole packet. Blocks until the
 *               packet has arrived (or the socket timeout expires).
 * Return      : PROTO_OK with *pkt filled in, PROTO_CLOSED, PROTO_ERR_IO or
 *               PROTO_ERR_BAD. After PROTO_ERR_BAD the stream position is
 *               unknown, so the caller must close the connection.
 */
int proto_recv_packet(int fd, Packet *pkt)
{
    unsigned char header[PROTO_HEADER_SIZE];
    unsigned char body[PROTO_MAX_BODY_SIZE];
    uint32_t type, length;
    int rc;

    rc = recv_all(fd, header, sizeof header, 1);
    if (rc != PROTO_OK)
        return rc;
    type = get_u32(header);
    length = get_u32(header + PROTO_U32_SIZE);
    if (!body_length_ok(type, length))
        return PROTO_ERR_BAD;

    rc = recv_all(fd, body, length, 0);
    if (rc != PROTO_OK)
        return rc;

    memset(pkt, 0, sizeof *pkt);
    pkt->type = type;
    return parse_body(pkt, body, length);
}

/*
 * Function    : is_name_char
 * Description : Characters allowed in a user name
 */
static int is_name_char(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
           || c == '_' || c == '.' || c == '-';
}

/*
 * Function    : proto_valid_username
 * Description : A user name has 1 to 31 characters from [A-Za-z0-9_.-]
 * Return      : 1 if valid, else 0
 */
int proto_valid_username(const char *name)
{
    size_t len;

    for (len = 0; name[len] != '\0'; len++)
    {
        if (len >= PROTO_NAME_SIZE - 1 || !is_name_char((unsigned char)name[len]))
            return 0;
    }
    return len > 0;
}

/*
 * Function    : proto_valid_password
 * Description : A password has 1 to 31 printable ASCII characters and no
 *               spaces (so it fits the "name password" lines of users.db)
 * Return      : 1 if valid, else 0
 */
int proto_valid_password(const char *password)
{
    size_t len;

    for (len = 0; password[len] != '\0'; len++)
    {
        unsigned char c = (unsigned char)password[len];

        if (len >= PROTO_PASS_SIZE - 1 || c <= ' ' || c > '~')
            return 0;
    }
    return len > 0;
}

/*
 * Function    : proto_text_is_blank
 * Description : True for an empty message or one made only of spaces / tabs
 */
int proto_text_is_blank(const char *text)
{
    for (; *text != '\0'; text++)
    {
        if (*text != ' ' && *text != '\t')
            return 0;
    }
    return 1;
}

/*
 * Function    : proto_copy_text
 * Description : Copies a string into a buffer of 'size' bytes, cutting it
 *               if necessary, always terminating it and zeroing the rest
 */
void proto_copy_text(char *dst, size_t size, const char *src)
{
    size_t len;

    if (size == 0)
        return;
    len = strnlen(src, size - 1);
    memcpy(dst, src, len);
    memset(dst + len, 0, size - len);
}
