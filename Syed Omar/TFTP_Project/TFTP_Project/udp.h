/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : udp.h
 *           Descriptions   : UDP socket helpers for the server and the client: binding,
 *                            sending, receiving with a timeout, address handling
 *           Objective      : Keep the socket system calls and their error handling in one place
 *           Usage          : #include "udp.h"
 *           Output         : None (library module)
 *
###############################################################################################*/

#ifndef TFTP_UDP_H
#define TFTP_UDP_H

#include <stddef.h>
#include <sys/types.h>
#include <netinet/in.h>

#define UDP_TIMEOUT         (-2)    /* udp_receive_packet(): nothing arrived in time    */
#define UDP_INTERRUPTED     (-3)    /* udp_receive_packet(): woken up by a signal       */
#define UDP_ADDR_TEXT       32      /* Buffer size for "255.255.255.255:65535"          */
#define UDP_PORT_MIN        1
#define UDP_PORT_MAX        65535

/* Test hook: percentage of outgoing packets silently dropped (see README) */
#define DROP_ENV            "TFTP_DROP_PERCENT"
#define DROP_PERCENT_MAX    90
#define PERCENT             100

int udp_bind_server(unsigned short port);
int udp_rebind_server(unsigned short *port, unsigned short first, unsigned short last);
int udp_bind_client(void);
int udp_send_packet(int sock, const void *buf, size_t len, const struct sockaddr_in *to);
ssize_t udp_receive_packet(int sock, void *buf, size_t size, struct sockaddr_in *from,
                           int timeout_ms);
int udp_same_endpoint(const struct sockaddr_in *a, const struct sockaddr_in *b);
int udp_same_host(const struct sockaddr_in *a, const struct sockaddr_in *b);
int udp_resolve(const char *host, unsigned short port, struct sockaddr_in *addr);
const char *udp_resolve_error(int code);
int udp_parse_port(const char *text, unsigned short *port);
char *udp_endpoint_text(const struct sockaddr_in *addr, char *buf, size_t size);

#endif
