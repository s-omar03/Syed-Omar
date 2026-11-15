/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : logger.h
 *           Descriptions   : Time-stamped server log on stderr
 *           Objective      : Let the operator follow connections, logins and message routing
 *                            (server option -v) without ever logging passwords or message text
 *           Usage          : #include "logger.h"
 *           Output         : "[YYYY-MM-DD HH:MM:SS] text" lines on stderr
 *
###############################################################################################*/

#ifndef LOGGER_H
#define LOGGER_H

#include <stddef.h>

#define LOG_LINE_SIZE        512    /* longest log line, longer ones are cut */
#define LOG_ERROR_TEXT_SIZE  128    /* buffer for log_errno_text()           */

void log_init(int verbose);
void log_info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
const char *log_errno_text(int err, char *buf, size_t size);

#endif
