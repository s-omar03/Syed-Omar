/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : tftp_probe.c
 *           Descriptions   : Test helper that speaks raw TFTP over UDP to a server on
 *                            127.0.0.1. It builds and decodes packets itself (it does not use
 *                            the project's tftp.c), so the tests check the bytes on the wire
 *                            independently of the code under test.
 *           Objective      : Let tests/run_tests.sh check error codes, the new-port scheme,
 *                            netascii on the wire, unknown transfer IDs and duplicate ACKs
 *           Usage          : tftp_probe raw    <port> <hex-bytes>
 *                            tftp_probe rrq    <port> <file> <mode>
 *                            tftp_probe wrq    <port> <file> <mode>
 *                            tftp_probe tid    <port> <file>   (file must be > 512 bytes)
 *                            tftp_probe dupack <port> <file>   (file must be > 1024 bytes)
 *                            PROBE_TIMEOUT_MS sets the reply timeout (default 3000)
 *           Output         : One line per received packet, e.g.
 *                            "reply from=20000 opcode=5 code=1 msg=File not found."
 *                            "reply from=20001 opcode=3 block=1 len=7 data=610d0a62..." or
 *                            "reply timeout"
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* clock_gettime(), poll() */

#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>

#define PROBE_HOST          "127.0.0.1"
#define TIMEOUT_ENV         "PROBE_TIMEOUT_MS"
#define DEFAULT_TIMEOUT_MS  3000
#define DUP_WINDOW_MS       600     /* Watch for repeated DATA 2 (server timeout is 1 s) */
#define MAX_PACKET          1024
#define HEADER              4
#define OPCODE_LEN          2
#define OP_RRQ              1
#define OP_WRQ              2
#define OP_DATA             3
#define OP_ACK              4
#define OP_ERROR            5
#define MS_PER_SECOND       1000
#define NS_PER_MS           1000000L
#define DECIMAL_BASE        10
#define HEX_BASE            16
#define BYTE_BITS           8
#define BYTE_MASK           0xFF
#define PRINTABLE_FIRST     0x20
#define PRINTABLE_LAST      0x7E

static int g_timeout_ms = DEFAULT_TIMEOUT_MS;

/*
 * Function    : now_ms
 * Description : Monotonic time in milliseconds
 */
static long long now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (long long)ts.tv_sec * MS_PER_SECOND + ts.tv_nsec / NS_PER_MS;
}

/*
 * Function    : open_socket
 * Description : New UDP socket on a kernel-chosen port (a new TID)
 */
static int open_socket(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    return sock;
}

/*
 * Function    : send_to
 * Description : Sends a datagram to PROBE_HOST:port
 */
static void send_to(int sock, unsigned short port, const unsigned char *buf, size_t len)
{
    struct sockaddr_in addr;

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, PROBE_HOST, &addr.sin_addr) != 1)
        exit(EXIT_FAILURE);
    if (sendto(sock, buf, len, 0, (struct sockaddr *)&addr, sizeof addr) < 0)
    {
        perror("sendto");
        exit(EXIT_FAILURE);
    }
}

/*
 * Function    : receive
 * Description : Waits up to timeout_ms for one datagram
 * Return      : Its length, or -1 on timeout
 */
static long receive(int sock, unsigned char *buf, size_t size, unsigned short *from_port,
                    int timeout_ms)
{
    struct pollfd pfd;
    struct sockaddr_in from;
    socklen_t len = sizeof from;
    ssize_t n;

    pfd.fd = sock;
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (timeout_ms < 0 || poll(&pfd, 1, timeout_ms) <= 0)
        return -1;
    n = recvfrom(sock, buf, size, 0, (struct sockaddr *)&from, &len);
    if (n < 0)
        return -1;
    *from_port = ntohs(from.sin_port);
    return (long)n;
}

/*
 * Function    : get16
 * Description : Reads a 16-bit value in network byte order
 */
static unsigned int get16(const unsigned char *p)
{
    return ((unsigned int)p[0] << BYTE_BITS) | p[1];
}

/*
 * Function    : put16
 * Description : Stores a 16-bit value in network byte order
 */
static void put16(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)((v >> BYTE_BITS) & BYTE_MASK);
    p[1] = (unsigned char)(v & BYTE_MASK);
}

/*
 * Function    : make_request
 * Description : Builds RRQ/WRQ by hand
 */
static size_t make_request(unsigned char *buf, unsigned int op, const char *file, const char *mode)
{
    size_t f = strlen(file), m = strlen(mode);

    if (OPCODE_LEN + f + 1 + m + 1 > MAX_PACKET)
        exit(EXIT_FAILURE);
    put16(buf, op);
    memcpy(buf + OPCODE_LEN, file, f + 1);
    memcpy(buf + OPCODE_LEN + f + 1, mode, m + 1);
    return OPCODE_LEN + f + 1 + m + 1;
}

/*
 * Function    : send_ack
 * Description : Sends ACK <block> to port
 */
static void send_ack(int sock, unsigned short port, unsigned int block)
{
    unsigned char buf[HEADER];

    put16(buf, OP_ACK);
    put16(buf + OPCODE_LEN, block);
    send_to(sock, port, buf, sizeof buf);
}

/*
 * Function    : finish
 * Description : Ends a server transfer at once with an ERROR packet, so no
 *               child process is left waiting for us
 */
static void finish(int sock, unsigned short port)
{
    static const char msg[] = "probe done";
    unsigned char buf[HEADER + sizeof msg];

    put16(buf, OP_ERROR);
    put16(buf + OPCODE_LEN, 0);
    memcpy(buf + HEADER, msg, sizeof msg);
    send_to(sock, port, buf, sizeof buf);
}

/*
 * Function    : print_packet
 * Description : Prints one received packet in "key=value" form
 */
static void print_packet(const char *label, const unsigned char *buf, long len,
                         unsigned short from)
{
    long i;

    if (len < 0)
    {
        printf("%s timeout\n", label);
        return;
    }
    printf("%s from=%u", label, (unsigned int)from);
    if (len < OPCODE_LEN)
    {
        printf(" len=%ld\n", len);
        return;
    }
    printf(" opcode=%u", get16(buf));
    if (get16(buf) == OP_DATA && len >= HEADER)
    {
        printf(" block=%u len=%ld data=", get16(buf + OPCODE_LEN), len - HEADER);
        for (i = HEADER; i < len; i++)
            printf("%02x", buf[i]);
    }
    else if (get16(buf) == OP_ACK && len >= HEADER)
    {
        printf(" block=%u", get16(buf + OPCODE_LEN));
    }
    else if (get16(buf) == OP_ERROR && len >= HEADER)
    {
        printf(" code=%u msg=", get16(buf + OPCODE_LEN));
        for (i = HEADER; i < len && buf[i] != '\0'; i++)
            putchar((buf[i] >= PRINTABLE_FIRST && buf[i] <= PRINTABLE_LAST) ? buf[i] : '?');
    }
    else
    {
        printf(" len=%ld", len);
    }
    putchar('\n');
}

/*
 * Function    : is_op
 * Description : Tells whether a received packet has the given opcode (and block)
 */
static int is_op(const unsigned char *buf, long len, unsigned int op, long block)
{
    if (len < HEADER || get16(buf) != op)
        return 0;
    return block < 0 || get16(buf + OPCODE_LEN) == (unsigned int)block;
}

/*
 * Function    : parse_hex
 * Description : Converts "0001ff..." to bytes
 * Return      : Number of bytes, exits on invalid input
 */
static size_t parse_hex(const char *hex, unsigned char *out, size_t size)
{
    size_t len = strlen(hex), i;

    if (len % 2 != 0 || len / 2 > size)
    {
        fprintf(stderr, "invalid hex string\n");
        exit(EXIT_FAILURE);
    }
    for (i = 0; i < len / 2; i++)
    {
        char pair[3] = { hex[2 * i], hex[2 * i + 1], '\0' };
        char *end;
        long v = strtol(pair, &end, HEX_BASE);

        if (*end != '\0')
        {
            fprintf(stderr, "invalid hex string\n");
            exit(EXIT_FAILURE);
        }
        out[i] = (unsigned char)v;
    }
    return len / 2;
}

/*
 * Function    : cmd_send
 * Description : Sends one packet and prints the first reply; a transfer the
 *               reply started is ended at once
 */
static int cmd_send(unsigned short port, const unsigned char *pkt, size_t len)
{
    unsigned char buf[MAX_PACKET];
    unsigned short from = 0;
    int sock = open_socket();
    long n;

    send_to(sock, port, pkt, len);
    n = receive(sock, buf, sizeof buf, &from, g_timeout_ms);
    print_packet("reply", buf, n, from);
    if (n >= OPCODE_LEN && get16(buf) != OP_ERROR)
        finish(sock, from);
    close(sock);
    return EXIT_SUCCESS;
}

/*
 * Function    : cmd_tid
 * Description : Unknown transfer ID: while a read transfer is running, a
 *               second socket sends an ACK to the transfer port. It must get
 *               error 5, and the real transfer must go on (DATA 2 follows).
 */
static int cmd_tid(unsigned short port, const char *file)
{
    unsigned char buf[MAX_PACKET], req[MAX_PACKET];
    unsigned short tport = 0, from = 0;
    int a = open_socket(), b = open_socket();
    long n;

    send_to(a, port, req, make_request(req, OP_RRQ, file, "octet"));
    n = receive(a, buf, sizeof buf, &tport, g_timeout_ms);
    print_packet("first", buf, n, tport);
    if (!is_op(buf, n, OP_DATA, 1))
        return EXIT_FAILURE;

    send_ack(b, tport, 1);
    n = receive(b, buf, sizeof buf, &from, g_timeout_ms);
    print_packet("stranger", buf, n, from);

    send_ack(a, tport, 1);
    n = receive(a, buf, sizeof buf, &from, g_timeout_ms);
    print_packet("continue", buf, n, from);

    finish(a, tport);
    close(a);
    close(b);
    return EXIT_SUCCESS;
}

/*
 * Function    : cmd_dupack
 * Description : Sorcerer's Apprentice check: ACK 1 is sent twice. A correct
 *               sender answers with exactly one DATA 2 (it must not resend
 *               DATA because of a duplicate ACK).
 */
static int cmd_dupack(unsigned short port, const char *file)
{
    unsigned char buf[MAX_PACKET], req[MAX_PACKET];
    unsigned short tport = 0, from = 0;
    int a = open_socket(), count = 0;
    long long end;
    long n;

    send_to(a, port, req, make_request(req, OP_RRQ, file, "octet"));
    n = receive(a, buf, sizeof buf, &tport, g_timeout_ms);
    print_packet("first", buf, n, tport);
    if (!is_op(buf, n, OP_DATA, 1))
        return EXIT_FAILURE;

    send_ack(a, tport, 1);
    send_ack(a, tport, 1);
    end = now_ms() + DUP_WINDOW_MS;
    while (now_ms() < end)
    {
        n = receive(a, buf, sizeof buf, &from, (int)(end - now_ms()));
        if (n < 0)
            break;
        if (from == tport && is_op(buf, n, OP_DATA, 2))
            count++;
    }
    printf("data2_count=%d\n", count);
    finish(a, tport);
    close(a);
    return EXIT_SUCCESS;
}

/*
 * Function    : usage
 * Description : Prints the command summary and exits
 */
static void usage(void)
{
    fprintf(stderr,
            "usage: tftp_probe raw <port> <hex>\n"
            "       tftp_probe rrq|wrq <port> <file> <mode>\n"
            "       tftp_probe tid|dupack <port> <file>\n");
    exit(EXIT_FAILURE);
}

/*
 * Function    : main
 * Description : Dispatches to a scenario
 */
int main(int argc, char *argv[])
{
    unsigned char pkt[MAX_PACKET];
    const char *env = getenv(TIMEOUT_ENV);
    char *end;
    long port;

    if (env != NULL)
        g_timeout_ms = (int)strtol(env, NULL, DECIMAL_BASE);
    if (argc < 4)
        usage();
    port = strtol(argv[2], &end, DECIMAL_BASE);
    if (*end != '\0' || port < 1 || port > 65535)
        usage();

    if (strcmp(argv[1], "raw") == 0)
        return cmd_send((unsigned short)port, pkt, parse_hex(argv[3], pkt, sizeof pkt));
    if ((strcmp(argv[1], "rrq") == 0 || strcmp(argv[1], "wrq") == 0) && argc == 5)
        return cmd_send((unsigned short)port, pkt,
                        make_request(pkt, (argv[1][0] == 'r') ? OP_RRQ : OP_WRQ, argv[3], argv[4]));
    if (strcmp(argv[1], "tid") == 0)
        return cmd_tid((unsigned short)port, argv[3]);
    if (strcmp(argv[1], "dupack") == 0)
        return cmd_dupack((unsigned short)port, argv[3]);
    usage();
    return EXIT_FAILURE;
}
