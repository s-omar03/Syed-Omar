/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : util.h
 *           Descriptions   : Small helpers used by both programs: monotonic clock, sleeping,
 *                            complete writes, port parsing, address formatting, escaping
 *           Objective      : Keep common low-level code in one place
 *           Usage          : #include "util.h"
 *           Output         : None (library module)
 *
###############################################################################################*/

#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <sys/socket.h>

#define UTIL_ADDR_TEXT_MAX   64          /* "[IPv6 address]:port" plus NUL     */
#define UTIL_PORT_MIN        1           /* Lowest TCP port accepted           */
#define UTIL_PORT_MAX        65535       /* Highest TCP port accepted          */
#define MS_PER_SECOND        1000LL
#define NS_PER_MS            1000000LL
#define ESCAPE_SEQ_LEN       4           /* Length of a "\xNN" escape          */

long long util_now_ms(void);
void   util_sleep_ms(long ms);
int    util_write_all(int fd, const void *buf, size_t length);
int    util_parse_port(const char *text, unsigned short *port);
void   util_format_addr(const struct sockaddr *sa, char *buf, size_t size);
size_t util_escape(const char *in, size_t length, char *out, size_t size);

#endif
