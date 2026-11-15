/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : protocol.h
 *           Descriptions   : Wire format shared by the client and the server: frame header,
 *                            message types, command codes, reply status codes and limits,
 *                            plus framed send/receive helpers with timeouts
 *           Objective      : Give both programs one definition of the request/reply format
 *           Usage          : #include "protocol.h"
 *           Output         : None (library module)
 *
###############################################################################################*/

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <signal.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Every message is one frame:
 *
 *   byte 0-1  magic   0x52 0x4D ("RM")
 *   byte 2    version PROTO_VERSION
 *   byte 3    type    MsgType
 *   byte 4-7  length  payload size in bytes, 0..PROTO_MAX_PAYLOAD
 *   byte 8..  payload
 *
 * Multi-byte numbers are in network byte order (big-endian).
 */
#define PROTO_MAGIC            0x524Du  /* "RM" - first two bytes of every frame      */
#define PROTO_VERSION          1u       /* Changed on incompatible format changes     */
#define PROTO_HEADER_SIZE      8u       /* magic(2) version(1) type(1) length(4)      */
#define PROTO_MAX_PAYLOAD      4096u    /* Largest payload of one frame               */
#define PROTO_MAX_NAME_LEN     64u      /* Longest process name accepted              */
#define PROTO_REQ_FIXED_SIZE   2u       /* Request payload: command(1) signal(1) name */
#define PROTO_IO_TIMEOUT_MS    10000    /* Rest of a started frame must arrive in time */
#define PROTO_WAIT_FOREVER     (-1)     /* Timeout value meaning "wait without limit" */
#define PROTO_NAME_EXTRA_CHARS "._-+:@/~,=#()[]"  /* Allowed besides letters/digits */

#define DEFAULT_PORT           6330         /* Port used in the spec's sample output  */
#define DEFAULT_SERVER_IP      "127.0.0.1"  /* Client default server address          */
#define DEFAULT_BIND_ADDR      "127.0.0.1"  /* Server default: local connections only */

/* Message types (byte 3 of the header) */
typedef enum
{
    MSG_HELLO   = 1,    /* server -> client: connection accepted, no payload       */
    MSG_REQUEST = 2,    /* client -> server: command(1) signal(1) name(1..64)      */
    MSG_ACK     = 3,    /* server -> client: request accepted, reply follows       */
    MSG_NACK    = 4,    /* server -> client: request rejected, payload = reason    */
    MSG_DATA    = 5,    /* server -> client: one part of the reply text            */
    MSG_END     = 6     /* server -> client: end of reply, payload = status(1)     */
} MsgType;

#define MSG_FIRST   MSG_HELLO
#define MSG_LAST    MSG_END

/* Remote commands (first byte of a request payload) */
typedef enum
{
    CMD_GET_MEM   = 1,
    CMD_GET_CPU   = 2,
    CMD_GET_PORTS = 3,
    CMD_GET_FD    = 4,
    CMD_KILL      = 5
} CommandCode;

#define CMD_FIRST   CMD_GET_MEM
#define CMD_LAST    CMD_KILL

/* Result of a request (payload of MSG_END) */
typedef enum
{
    STATUS_OK        = 0,   /* everything worked                          */
    STATUS_NOT_FOUND = 1,   /* no process with that name                  */
    STATUS_PARTIAL   = 2,   /* worked for some processes, failed for some */
    STATUS_FAILED    = 3    /* failed for every matching process          */
} ReplyStatus;

#define STATUS_LAST STATUS_FAILED

/* Signal numbers on the wire (traditional values, mapped to the local ones) */
#define PROTO_SIG_HUP      1
#define PROTO_SIG_INT      2
#define PROTO_SIG_QUIT     3
#define PROTO_SIG_KILL     9
#define PROTO_SIG_TERM     15
#define PROTO_SIG_DEFAULT  PROTO_SIG_TERM

/* Return codes of the I/O functions */
typedef enum
{
    PROTO_OK = 0,
    PROTO_ERR_CLOSED,       /* peer closed the connection between frames  */
    PROTO_ERR_TRUNCATED,    /* peer closed the connection inside a frame  */
    PROTO_ERR_TIMEOUT,      /* nothing (or not enough) arrived in time    */
    PROTO_ERR_IO,           /* system call failed, see errno              */
    PROTO_ERR_INTERRUPTED,  /* the interrupt flag was set by a signal     */
    PROTO_ERR_MAGIC,        /* frame does not start with PROTO_MAGIC      */
    PROTO_ERR_VERSION,      /* unsupported protocol version               */
    PROTO_ERR_TYPE,         /* unknown message type                       */
    PROTO_ERR_LENGTH        /* payload larger than allowed                */
} ProtoResult;

/* One received frame. The payload is always followed by a '\0'. */
typedef struct
{
    uint8_t  type;
    uint32_t length;
    unsigned char payload[PROTO_MAX_PAYLOAD + 1];
} Frame;

/* Decoded request */
typedef struct
{
    int  command;                          /* CommandCode                    */
    int  signal;                           /* PROTO_SIG_xxx, 0 if not kill   */
    char name[PROTO_MAX_NAME_LEN + 1];     /* process name, NUL-terminated   */
} Request;

void proto_set_interrupt_flag(volatile sig_atomic_t *flag);
int  proto_set_nonblocking(int fd);

int  proto_send(int fd, int type, const void *payload, size_t length);
int  proto_recv(int fd, Frame *frame, int idle_timeout_ms);
void proto_drain(int fd, int timeout_ms, size_t max_bytes);
const char *proto_result_text(int result);

size_t proto_encode_request(const Request *req, unsigned char *buf, size_t size);
int  proto_decode_request(const unsigned char *payload, size_t length, Request *req,
                          const char **why);

int  proto_valid_name(const char *name, size_t length);
int  proto_command_from_name(const char *word);
const char *proto_command_name(int command);

int  proto_signal_parse(const char *text, int *wire_signal);
const char *proto_signal_name(int wire_signal);
int  proto_signal_native(int wire_signal);

const char *proto_status_text(int status);

#endif
