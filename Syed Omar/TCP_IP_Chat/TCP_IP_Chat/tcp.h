/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : tcp.h
 *           Descriptions   : Small helpers around the BSD socket API used by the server and
 *                            the client
 *           Objective      : Keep socket set-up details (address resolution, socket options,
 *                            port parsing) out of the program logic
 *           Usage          : #include "tcp.h"
 *           Output         : None (library module)
 *
###############################################################################################*/

#ifndef TCP_H
#define TCP_H

#include <stddef.h>
#include <stdint.h>

#define TCP_PEER_NAME_SIZE   64     /* "address:port" text for log messages */
#define TCP_ERROR_TEXT_SIZE  128    /* buffer for connection error texts    */

int tcp_listen(uint16_t port, int backlog);
int tcp_connect(const char *host, const char *port, char *error, size_t error_size);
int tcp_set_send_timeout(int fd, int seconds);
int tcp_set_recv_timeout(int fd, int seconds);
int tcp_set_nonblocking(int fd, int enable);
void tcp_peer_name(int fd, char *buf, size_t size);
int tcp_parse_port(const char *text, uint16_t *port);

#endif
