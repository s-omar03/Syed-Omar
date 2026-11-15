/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : tcp.c
 *           Descriptions   : Socket helpers: listening socket, client connection through
 *                            getaddrinfo(), send timeout, non-blocking mode, peer address
 *                            text and port number parsing
 *           Objective      : Hide the socket set-up boilerplate behind a few checked calls
 *           Usage          : Used by server.c, session.c and client.c through tcp.h
 *           Output         : None (library module)
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* getaddrinfo(), getnameinfo() */

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include "tcp.h"

#define TCP_PORT_MIN            1
#define TCP_PORT_MAX            65535
#define TCP_SERVICE_TEXT_SIZE   8       /* "65535" plus NUL, rounded up */
#define DECIMAL_BASE            10

/*
 * Function    : close_keep_errno
 * Description : Closes a descriptor without losing the errno of an earlier
 *               failure
 */
static void close_keep_errno(int fd)
{
    int saved = errno;

    close(fd);
    errno = saved;
}

/*
 * Function    : tcp_listen
 * Description : Creates an IPv4 TCP socket listening on every interface.
 *               SO_REUSEADDR lets a restarted server bind again at once.
 * Return      : Listening descriptor, or -1 with errno set
 */
int tcp_listen(uint16_t port, int backlog)
{
    struct sockaddr_in addr;
    int fd, on = 1;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) != 0)
    {
        close_keep_errno(fd);
        return -1;
    }

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(fd, backlog) != 0)
    {
        close_keep_errno(fd);
        return -1;
    }
    return fd;
}

/*
 * Function    : tcp_connect
 * Description : Resolves host and port (IPv4 or IPv6, name or address) and
 *               connects to the first address that accepts the connection
 * Return      : Connected descriptor, or -1 with a reason in 'error'
 */
int tcp_connect(const char *host, const char *port, char *error, size_t error_size)
{
    struct addrinfo hints, *list, *ai;
    int fd = -1, rc, last_errno = 0;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    rc = getaddrinfo(host, port, &hints, &list);
    if (rc != 0)
    {
        snprintf(error, error_size, "%s", gai_strerror(rc));
        return -1;
    }

    for (ai = list; ai != NULL; ai = ai->ai_next)
    {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
        {
            last_errno = errno;
            continue;
        }
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        last_errno = errno;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(list);

    if (fd < 0)
        snprintf(error, error_size, "%s", strerror(last_errno != 0 ? last_errno : ECONNREFUSED));
    return fd;
}

/*
 * Function    : set_timeout
 * Description : Sets SO_SNDTIMEO or SO_RCVTIMEO to 'seconds'
 * Return      : 0 on success, -1 on error
 */
static int set_timeout(int fd, int option, int seconds)
{
    struct timeval tv;

    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    return setsockopt(fd, SOL_SOCKET, option, &tv, sizeof tv);
}

/*
 * Function    : tcp_set_send_timeout
 * Description : Makes a blocking send() give up after 'seconds', so a
 *               peer that stops reading cannot block the sender forever
 * Return      : 0 on success, -1 on error
 */
int tcp_set_send_timeout(int fd, int seconds)
{
    return set_timeout(fd, SO_SNDTIMEO, seconds);
}

/*
 * Function    : tcp_set_recv_timeout
 * Description : Makes a blocking recv() give up after 'seconds', so a
 *               packet that stops half-way cannot block the reader forever
 * Return      : 0 on success, -1 on error
 */
int tcp_set_recv_timeout(int fd, int seconds)
{
    return set_timeout(fd, SO_RCVTIMEO, seconds);
}

/*
 * Function    : tcp_set_nonblocking
 * Description : Switches O_NONBLOCK on (enable != 0) or off
 * Return      : 0 on success, -1 on error
 */
int tcp_set_nonblocking(int fd, int enable)
{
    int flags = fcntl(fd, F_GETFL);

    if (flags < 0)
        return -1;
    flags = enable ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return fcntl(fd, F_SETFL, flags);
}

/*
 * Function    : tcp_peer_name
 * Description : Writes "address:port" of the remote end into buf
 *               ("unknown" if it cannot be determined)
 */
void tcp_peer_name(int fd, char *buf, size_t size)
{
    struct sockaddr_storage addr;
    socklen_t len = sizeof addr;
    char host[INET6_ADDRSTRLEN], service[TCP_SERVICE_TEXT_SIZE];

    if (getpeername(fd, (struct sockaddr *)&addr, &len) != 0
        || getnameinfo((struct sockaddr *)&addr, len, host, sizeof host, service, sizeof service,
                       NI_NUMERICHOST | NI_NUMERICSERV) != 0)
    {
        snprintf(buf, size, "unknown");
        return;
    }
    snprintf(buf, size, "%s:%s", host, service);
}

/*
 * Function    : tcp_parse_port
 * Description : Converts a decimal port number (1 - 65535)
 * Return      : 0 and *port on success, -1 if the text is not a valid port
 */
int tcp_parse_port(const char *text, uint16_t *port)
{
    char *end;
    long value;

    errno = 0;
    value = strtol(text, &end, DECIMAL_BASE);
    if (errno != 0 || end == text || *end != '\0' || value < TCP_PORT_MIN || value > TCP_PORT_MAX)
        return -1;
    *port = (uint16_t)value;
    return 0;
}
