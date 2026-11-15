/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : util.c
 *           Descriptions   : Small helpers used by both programs: monotonic clock, sleeping,
 *                            complete writes, port parsing, address formatting, escaping
 *           Objective      : Keep common low-level code in one place
 *           Usage          : Linked into both ./server and ./client
 *           Output         : None (library module)
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* clock_gettime(), nanosleep(), inet_ntop() */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "util.h"

#define DECIMAL_BASE    10
#define HEX_DIGITS      "0123456789abcdef"
#define ASCII_FIRST     0x20        /* First printable ASCII character (space) */
#define ASCII_LAST      0x7e        /* Last printable ASCII character ('~')    */

/*
 * Function    : util_now_ms
 * Description : Monotonic clock in milliseconds (not affected by changes of
 *               the wall-clock time), used for timeouts and measurements
 * Return      : Milliseconds since an arbitrary fixed point
 */
long long util_now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (long long)ts.tv_sec * MS_PER_SECOND + (long long)ts.tv_nsec / NS_PER_MS;
}

/*
 * Function    : util_sleep_ms
 * Description : Sleeps for the given number of milliseconds. A signal that
 *               interrupts the sleep does not shorten it.
 */
void util_sleep_ms(long ms)
{
    struct timespec req, rem;

    if (ms <= 0)
        return;
    req.tv_sec = (time_t)(ms / MS_PER_SECOND);
    req.tv_nsec = (long)((ms % MS_PER_SECOND) * NS_PER_MS);
    while (nanosleep(&req, &rem) != 0 && errno == EINTR)
        req = rem;
}

/*
 * Function    : util_write_all
 * Description : Writes the whole buffer to a (blocking) descriptor, retrying
 *               after partial writes and EINTR
 * Return      : 0 on success, -1 on error (errno set)
 */
int util_write_all(int fd, const void *buf, size_t length)
{
    const char *p = buf;

    while (length > 0)
    {
        ssize_t n = write(fd, p, length);

        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += n;
        length -= (size_t)n;
    }
    return 0;
}

/*
 * Function    : util_parse_port
 * Description : Converts a decimal port number (1-65535) from text. Signs,
 *               spaces and trailing characters are rejected.
 * Return      : 0 and *port on success, -1 if the text is not a valid port
 */
int util_parse_port(const char *text, unsigned short *port)
{
    char *end;
    long value;

    if (text == NULL || text[0] < '0' || text[0] > '9')
        return -1;
    errno = 0;
    value = strtol(text, &end, DECIMAL_BASE);
    if (errno != 0 || *end != '\0' || value < UTIL_PORT_MIN || value > UTIL_PORT_MAX)
        return -1;
    *port = (unsigned short)value;
    return 0;
}

/*
 * Function    : util_format_addr
 * Description : Formats an IPv4 address as "a.b.c.d:port" and an IPv6
 *               address as "[addr]:port"
 */
void util_format_addr(const struct sockaddr *sa, char *buf, size_t size)
{
    char ip[INET6_ADDRSTRLEN];

    if (size == 0)
        return;
    if (sa->sa_family == AF_INET)
    {
        struct sockaddr_in in4;

        memcpy(&in4, sa, sizeof in4);
        if (inet_ntop(AF_INET, &in4.sin_addr, ip, sizeof ip) == NULL)
            snprintf(ip, sizeof ip, "?");
        snprintf(buf, size, "%s:%u", ip, (unsigned)ntohs(in4.sin_port));
    }
    else if (sa->sa_family == AF_INET6)
    {
        struct sockaddr_in6 in6;

        memcpy(&in6, sa, sizeof in6);
        if (inet_ntop(AF_INET6, &in6.sin6_addr, ip, sizeof ip) == NULL)
            snprintf(ip, sizeof ip, "?");
        snprintf(buf, size, "[%s]:%u", ip, (unsigned)ntohs(in6.sin6_port));
    }
    else
    {
        snprintf(buf, size, "unknown address");
    }
}

/*
 * Function    : util_escape
 * Description : Copies 'length' bytes to 'out', replacing every byte that is
 *               not printable ASCII by "\xNN". The result is cut short (never
 *               in the middle of an escape) if it does not fit in 'size'.
 * Return      : Length of the text written to 'out' (always NUL-terminated)
 */
size_t util_escape(const char *in, size_t length, char *out, size_t size)
{
    size_t i, o = 0;

    if (size == 0)
        return 0;
    for (i = 0; i < length; i++)
    {
        unsigned char c = (unsigned char)in[i];

        if (c >= ASCII_FIRST && c <= ASCII_LAST)
        {
            if (o + 1 >= size)
                break;
            out[o++] = (char)c;
        }
        else
        {
            if (o + ESCAPE_SEQ_LEN >= size)
                break;
            out[o++] = '\\';
            out[o++] = 'x';
            out[o++] = HEX_DIGITS[c >> 4];
            out[o++] = HEX_DIGITS[c & 0x0f];
        }
    }
    out[o] = '\0';
    return o;
}
