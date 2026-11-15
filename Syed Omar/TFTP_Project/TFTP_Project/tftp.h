/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : tftp.h
 *           Descriptions   : TFTP (RFC 1350) protocol constants, packet structure, packet
 *                            formation/parsing API and the transfer engine shared by the
 *                            client and the server
 *           Objective      : One implementation of the protocol rules (lock-step DATA/ACK,
 *                            timeouts, retransmission, transfer IDs) for both programs
 *           Usage          : #include "tftp.h"
 *           Output         : None (library module)
 *
###############################################################################################*/

#ifndef TFTP_H
#define TFTP_H

#include <stddef.h>
#include <signal.h>
#include <netinet/in.h>
#include "file.h"

/* Packet sizes (RFC 1350) */
#define BLIMIT          512                     /* Data bytes in a full DATA packet        */
#define OPCODE_SIZE     2                       /* Opcode field                            */
#define HEADER_SIZE     4                       /* Opcode + block number or error code     */
#define PACKETSIZE      (HEADER_SIZE + BLIMIT)  /* Largest valid TFTP packet (516)         */
#define RECV_BUFFER     (PACKETSIZE + 1)        /* One spare byte detects oversized packets */

/* Ports */
#define TFTP_STANDARD_PORT       69      /* Well-known TFTP port (needs root)             */
#define TFTP_DEFAULT_PORT        6969    /* Default of this project: no root needed       */
#define TFTP_TRANSFER_PORT_FIRST 20000   /* First port of a server child (spec)           */
#define TFTP_TRANSFER_PORT_LAST  65535   /* Port numbers wrap back to the first one       */

typedef unsigned short opcode_t; /* TFTP Operation Code */
#define OPCODE_RRQ   1
#define IS_RRQ(op)   ((op) == OPCODE_RRQ) /* Evaluates as true or false */
#define OPCODE_WRQ   2
#define IS_WRQ(op)   ((op) == OPCODE_WRQ)
#define OPCODE_DATA  3
#define IS_DATA(op)  ((op) == OPCODE_DATA)
#define OPCODE_ACK   4
#define IS_ACK(op)   ((op) == OPCODE_ACK)
#define OPCODE_ERROR 5
#define IS_ERROR(op) ((op) == OPCODE_ERROR)

typedef unsigned short ecode_t; /* TFTP Error code (RFC 1350, section 5 and appendix) */
#define ECODE_0     0
#define ESTRING_0   "Not defined, see error message (if any)."
#define ECODE_1     1
#define ESTRING_1   "File not found."
#define ECODE_2     2
#define ESTRING_2   "Access violation."
#define ECODE_3     3
#define ESTRING_3   "Disk full or allocation exceeded."
#define ECODE_4     4
#define ESTRING_4   "Illegal TFTP operation."
#define ECODE_5     5
#define ESTRING_5   "Unknown transfer ID."
#define ECODE_6     6
#define ESTRING_6   "File already exists."
#define ECODE_7     7
#define ESTRING_7   "No such user."
#define ECODE_MAX   ECODE_7

/* Transfer operation modes (names are case-insensitive on the wire) */
#define MODE_NETASCII "netascii"
#define MODE_OCTET    "octet"
#define MODE_MAIL     "mail"

typedef enum
{
    TFTP_MODE_INVALID = -1,     /* Not a mode name                                 */
    TFTP_MODE_NETASCII,         /* Text: LF <-> CR LF, CR <-> CR NUL               */
    TFTP_MODE_OCTET,            /* Raw 8-bit bytes                                 */
    TFTP_MODE_MAIL              /* Obsolete (RFC 1350): recognised, never served   */
} tftp_mode_t;

/* Timeouts and retransmission */
#define TIMEOUT         1       /* Seconds to wait for a packet before retransmitting  */
#define TIMEOUT_LIMIT   10      /* Retransmissions of one packet before giving up      */
#define DALLY_FACTOR    2       /* After the final ACK, linger DALLY_FACTOR timeouts   */
#define MS_PER_SECOND   1000
#define TIMEOUT_ENV     "TFTP_TIMEOUT_MS"   /* Test hook: timeout in milliseconds    */
#define TIMEOUT_MS_MIN  10
#define TIMEOUT_MS_MAX  60000

typedef unsigned short bnum_t; /* TFTP Block number, for data packets (wraps 65535 -> 0) */

/* Storage of the data extracted from a received packet buffer */
typedef struct tftp_packet
{
    opcode_t opcode;
    char filename[PACKETSIZE];      /* RRQ/WRQ: NUL-terminated file name            */
    char mode[PACKETSIZE];          /* RRQ/WRQ: NUL-terminated mode, as received     */
    bnum_t blocknum;                /* DATA/ACK                                      */
    unsigned char data[BLIMIT];     /* DATA                                          */
    size_t data_length;             /* DATA: 0 to BLIMIT bytes                       */
    ecode_t ecode;                  /* ERROR                                         */
    char estring[PACKETSIZE];       /* ERROR: message, non-printable bytes replaced  */
} packet_t;

/* Results of the transfer functions */
#define TFTP_OK             0   /* Transfer complete                                  */
#define TFTP_ERR_TIMEOUT    1   /* Peer stopped answering                             */
#define TFTP_ERR_REMOTE     2   /* Peer sent an ERROR packet                          */
#define TFTP_ERR_LOCAL      3   /* Local file error (the peer was sent an ERROR)      */
#define TFTP_ERR_PROTOCOL   4   /* Peer sent an illegal packet (peer was told)        */
#define TFTP_ERR_SOCKET     5   /* Socket error                                       */
#define TFTP_ERR_CANCELLED  6   /* Cancelled by the user (peer was told)              */

/* Called after every block that was acknowledged (sender) or written (receiver) */
typedef void (*tftp_progress_fn)(void *ctx, unsigned long long blocks);

/* State of one transfer between two transfer IDs (UDP ports) */
typedef struct tftp_session
{
    int sock;                           /* Our socket: our TID                          */
    struct sockaddr_in peer;            /* Peer address; its port is the peer's TID     */
    int peer_locked;                    /* 0 until the peer's first packet fixes its TID */
    int request_pending;                /* 1 while an RRQ/WRQ is unanswered (client)    */
    int timeout_ms;                     /* Retransmission timeout                       */
    int dally;                          /* Receiver: linger to re-ACK a lost final ACK  */
    volatile sig_atomic_t *cancel;      /* Optional: non-zero cancels the transfer      */
    tftp_progress_fn progress;          /* Optional progress callback                   */
    void *progress_ctx;
    unsigned char last[PACKETSIZE];     /* Last packet sent, kept for retransmission    */
    size_t last_len;
    unsigned long long blocks;          /* DATA blocks transferred                      */
    unsigned long long bytes;           /* Data bytes on the wire                       */
    unsigned long long retransmissions; /* Packets sent again after a timeout           */
    ecode_t ecode;                      /* Error code sent or received on failure       */
    char message[PACKETSIZE];           /* Readable reason of a failure                 */
} tftp_session_t;

/* Packet parsing */
int packet_parse(const unsigned char *buf, size_t len, packet_t *pkt);
int packet_peek_opcode(const unsigned char *buf, size_t len, opcode_t *opcode);

/* Packet formation: each returns the packet length, 0 if it does not fit */
size_t packet_form_rrq(unsigned char *buf, const char *filename, const char *mode);
size_t packet_form_wrq(unsigned char *buf, const char *filename, const char *mode);
size_t packet_form_data(unsigned char *buf, bnum_t block, const unsigned char *data, size_t len);
size_t packet_form_ack(unsigned char *buf, bnum_t block);
size_t packet_form_error(unsigned char *buf, ecode_t code, const char *message);

/* Helpers */
const char *tftp_error_string(ecode_t code);
ecode_t tftp_errno_to_ecode(int err);
tftp_mode_t tftp_parse_mode(const char *name);
const char *tftp_mode_name(tftp_mode_t mode);
int tftp_timeout_ms(void);
void tftp_printable(const char *in, char *out, size_t size);
int tftp_send_error(int sock, const struct sockaddr_in *to, ecode_t code, const char *message);

/* Transfer engine */
void tftp_session_init(tftp_session_t *s, int sock, const struct sockaddr_in *peer, int locked);
int tftp_send_request(tftp_session_t *s, opcode_t opcode, const char *filename, tftp_mode_t mode);
int tftp_send_file(tftp_session_t *s, tftp_file_t *f);
int tftp_receive_file(tftp_session_t *s, tftp_file_t *f);
const char *tftp_result_string(int result);

#endif
