/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : udp.c
 *           Descriptions   : Functions to bind udp sockets for server and clients, to send and
 *                            to receive datagrams (with a timeout) and to handle addresses
 *           Objective      : Create a UDP socket and bind with given IP address and PORT number
 *           Usage          : udp_bind_server() for the listening socket, udp_rebind_server()
 *                            for a transfer port of the server, udp_bind_client() for the client
 *           Output         : None (library module); functions return -1 and set errno on error
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* getaddrinfo(), poll() */

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include "udp.h"

#define DECIMAL_BASE    10
#define DOTTED_CHARS    "0123456789."   /* Characters of a dotted IPv4 address */
#define RNG_PID_SHIFT   16      /* Mixes the pid into the seed          */
#define XORSHIFT_A      13      /* xorshift32 shift constants (Marsaglia) */
#define XORSHIFT_B      17
#define XORSHIFT_C      5

/*
 * Function    : bind_any
 * Description : Binds a socket to the given port on all local IPv4 addresses
 * Return      : 0 on success, -1 with errno set on error
 */
static int bind_any(int sock, unsigned short port)
{
    struct sockaddr_in addr;

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    return bind(sock, (struct sockaddr *)&addr, sizeof addr);
}

/*
 * Function    : open_bound
 * Description : Creates a UDP socket bound to 'port' (0: any free port)
 * Return      : Socket descriptor, or -1 with errno set
 */
static int open_bound(unsigned short port)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0)
        return -1;
    if (bind_any(sock, port) != 0)
    {
        int saved = errno;

        close(sock);
        errno = saved;
        return -1;
    }
    return sock;
}

/*
 * Function    : udp_bind_server
 * Description : Creates the server's listening socket on the given port
 * Return      : Socket descriptor, or -1 with errno set
 */
int udp_bind_server(unsigned short port)
{
    return open_bound(port);
}

/*
 * Function    : udp_rebind_server
 * Description : Creates the socket of one server transfer (its new TID).
 *               Tries *port first; a port that is in use is skipped and the
 *               next one tried, wrapping from 'last' back to 'first'.
 * Return      : Socket descriptor with *port set to the bound port, or -1
 *               with errno set (EADDRINUSE if every port is busy)
 */
int udp_rebind_server(unsigned short *port, unsigned short first, unsigned short last)
{
    unsigned long attempts = (unsigned long)last - first + 1;
    unsigned short candidate = *port;
    int sock;

    if (candidate < first || candidate > last)
        candidate = first;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return -1;

    while (attempts-- > 0)
    {
        if (bind_any(sock, candidate) == 0)
        {
            *port = candidate;
            return sock;
        }
        if (errno != EADDRINUSE)
            break;
        candidate = (candidate == last) ? first : (unsigned short)(candidate + 1);
    }

    {
        int saved = errno;

        close(sock);
        errno = saved;
    }
    return -1;
}

/*
 * Function    : udp_bind_client
 * Description : Creates a client socket on a free port chosen by the kernel.
 *               A new socket is used for every transfer, so every transfer
 *               gets its own TID as RFC 1350 asks.
 * Return      : Socket descriptor, or -1 with errno set
 */
int udp_bind_client(void)
{
    return open_bound(0);
}

/*
 * Function    : drop_percent
 * Description : Reads the TFTP_DROP_PERCENT test hook once (0 if unset or
 *               invalid, capped at DROP_PERCENT_MAX)
 */
static int drop_percent(void)
{
    static int value = -1;

    if (value < 0)
    {
        const char *text = getenv(DROP_ENV);
        char *end;
        long n;

        value = 0;
        if (text != NULL && text[0] != '\0')
        {
            errno = 0;
            n = strtol(text, &end, DECIMAL_BASE);
            if (errno == 0 && *end == '\0' && n > 0)
                value = (n > DROP_PERCENT_MAX) ? DROP_PERCENT_MAX : (int)n;
        }
    }
    return value;
}

/*
 * Function    : should_drop
 * Description : Decides whether to simulate the loss of the next packet.
 *               Uses a small xorshift generator seeded per process, so the
 *               server's child processes do not all drop the same packets.
 */
static int should_drop(void)
{
    static unsigned int state;
    static pid_t seeded_pid;
    int percent = drop_percent();
    pid_t pid;

    if (percent == 0)
        return 0;

    pid = getpid();
    if (pid != seeded_pid || state == 0)
    {
        seeded_pid = pid;
        state = (unsigned int)time(NULL) ^ ((unsigned int)pid << RNG_PID_SHIFT) ^ (unsigned int)pid;
        if (state == 0)
            state = 1;
    }
    state ^= state << XORSHIFT_A;
    state ^= state >> XORSHIFT_B;
    state ^= state << XORSHIFT_C;
    return (int)(state % PERCENT) < percent;
}

/*
 * Function    : udp_send_packet
 * Description : Sends one datagram, retrying if a signal interrupts the call
 * Return      : 0 on success, -1 with errno set on error
 */
int udp_send_packet(int sock, const void *buf, size_t len, const struct sockaddr_in *to)
{
    ssize_t n;

    if (should_drop())
        return 0;           /* Simulated loss: pretend the packet was sent */

    do
    {
        n = sendto(sock, buf, len, 0, (const struct sockaddr *)to, sizeof *to);
    } while (n < 0 && errno == EINTR);

    if (n < 0)
        return -1;
    if ((size_t)n != len)
    {
        errno = EMSGSIZE;
        return -1;
    }
    return 0;
}

/*
 * Function    : udp_receive_packet
 * Description : Waits up to timeout_ms for a datagram and receives it.
 *               A datagram larger than 'size' is truncated by the kernel, so
 *               callers pass a buffer one byte larger than the largest valid
 *               packet to detect oversized packets.
 * Return      : Datagram length (may be 0), UDP_TIMEOUT, UDP_INTERRUPTED,
 *               or -1 with errno set on error
 */
ssize_t udp_receive_packet(int sock, void *buf, size_t size, struct sockaddr_in *from,
                           int timeout_ms)
{
    struct pollfd pfd;
    socklen_t len = sizeof *from;
    ssize_t n;
    int rc;

    pfd.fd = sock;
    pfd.events = POLLIN;
    pfd.revents = 0;
    rc = poll(&pfd, 1, timeout_ms);
    if (rc < 0)
        return (errno == EINTR) ? UDP_INTERRUPTED : -1;
    if (rc == 0)
        return UDP_TIMEOUT;

    memset(from, 0, sizeof *from);
    n = recvfrom(sock, buf, size, 0, (struct sockaddr *)from, &len);
    if (n < 0)
    {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            return UDP_INTERRUPTED;
        return -1;
    }
    if (len < sizeof *from || from->sin_family != AF_INET)
        return UDP_INTERRUPTED;    /* Not an IPv4 sender: ignore it */
    return n;
}

/*
 * Function    : udp_same_endpoint
 * Description : Compares IP address and port (i.e. the transfer ID)
 */
int udp_same_endpoint(const struct sockaddr_in *a, const struct sockaddr_in *b)
{
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

/*
 * Function    : udp_same_host
 * Description : Compares the IP addresses only
 */
int udp_same_host(const struct sockaddr_in *a, const struct sockaddr_in *b)
{
    return a->sin_addr.s_addr == b->sin_addr.s_addr;
}

/*
 * Function    : udp_resolve
 * Description : Converts a dotted IPv4 address or a host name to an address.
 *               Text made of digits and dots is checked as an address only,
 *               so a typo such as 999.1.1.1 never waits for a DNS lookup.
 * Return      : 0 on success, otherwise a getaddrinfo() error code (see
 *               udp_resolve_error())
 */
int udp_resolve(const char *host, unsigned short port, struct sockaddr_in *addr)
{
    struct addrinfo hints, *result = NULL;
    int rc;

    /* Digits and dots only: must be a dotted IPv4 address, never a DNS name */
    if (strspn(host, DOTTED_CHARS) == strlen(host))
    {
        memset(addr, 0, sizeof *addr);
        addr->sin_family = AF_INET;
        addr->sin_port = htons(port);
        return (inet_pton(AF_INET, host, &addr->sin_addr) == 1) ? 0 : EAI_NONAME;
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    rc = getaddrinfo(host, NULL, &hints, &result);
    if (rc != 0)
        return rc;
    if (result == NULL || result->ai_addrlen < sizeof *addr)
    {
        freeaddrinfo(result);
        return EAI_FAIL;
    }
    memcpy(addr, result->ai_addr, sizeof *addr);
    addr->sin_port = htons(port);
    freeaddrinfo(result);
    return 0;
}

/*
 * Function    : udp_resolve_error
 * Description : Text for an error code of udp_resolve()
 */
const char *udp_resolve_error(int code)
{
    return gai_strerror(code);
}

/*
 * Function    : udp_parse_port
 * Description : Parses a port number (decimal digits only, 1 to 65535)
 * Return      : 0 on success, -1 if the text is not a valid port
 */
int udp_parse_port(const char *text, unsigned short *port)
{
    unsigned long value = 0;
    const char *p;

    if (text[0] == '\0')
        return -1;
    for (p = text; *p != '\0'; p++)
    {
        if (*p < '0' || *p > '9')
            return -1;
        value = value * DECIMAL_BASE + (unsigned long)(*p - '0');
        if (value > UDP_PORT_MAX)
            return -1;
    }
    if (value < UDP_PORT_MIN)
        return -1;
    *port = (unsigned short)value;
    return 0;
}

/*
 * Function    : udp_endpoint_text
 * Description : Formats an address as "a.b.c.d:port" for messages
 * Return      : buf
 */
char *udp_endpoint_text(const struct sockaddr_in *addr, char *buf, size_t size)
{
    char ip[INET_ADDRSTRLEN];

    if (inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof ip) == NULL)
        snprintf(ip, sizeof ip, "?");
    snprintf(buf, size, "%s:%u", ip, (unsigned int)ntohs(addr->sin_port));
    return buf;
}
