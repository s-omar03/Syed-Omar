/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : session.h
 *           Descriptions   : Server side of one client connection and the server's activity log
 *           Objective      : Read requests, run them and send the replies until the client
 *                            disconnects
 *           Usage          : #include "session.h"
 *           Output         : None (library module)
 *
###############################################################################################*/

#ifndef SESSION_H
#define SESSION_H

#define SESSION_IDLE_TIMEOUT_MS   (30 * 60 * 1000)  /* Drop clients idle for 30 minutes   */
#define SESSION_DRAIN_TIMEOUT_MS  500               /* Linger before closing on an error  */
#define SESSION_DRAIN_MAX_BYTES   65536             /* ... reading at most this much      */
#define REPLY_MAX_SIZE            (256 * 1024)      /* Longest reply text                 */
#define LOG_LINE_MAX              512               /* Longest activity log line          */
#define LOG_PREFIX                "RMS Server: "    /* Start of every log line (Fig 5.5)  */

#if defined(__GNUC__)
#define LOG_PRINTF_CHECK __attribute__((format(printf, 1, 2)))
#else
#define LOG_PRINTF_CHECK
#endif

void server_log(const char *fmt, ...) LOG_PRINTF_CHECK;
void session_run(int fd, const char *peer);

#endif
