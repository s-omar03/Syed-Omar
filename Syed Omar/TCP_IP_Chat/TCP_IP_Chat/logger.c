/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : logger.c
 *           Descriptions   : Time-stamped, thread-safe log lines on stderr. Activity lines are
 *                            printed only in verbose mode, errors always.
 *           Objective      : Give the background server a simple activity log
 *           Usage          : Used by server.c, session.c and chat_room.c through logger.h
 *           Output         : "[YYYY-MM-DD HH:MM:SS] text" lines on stderr
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* localtime_r(), XSI strerror_r() */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "logger.h"

#define TIME_TEXT_SIZE   32
#define TIME_FORMAT      "%Y-%m-%d %H:%M:%S"

static int log_verbose = 0;     /* Set once by log_init() before any thread starts */

/*
 * Function    : log_init
 * Description : Selects whether log_info() lines are printed
 */
void log_init(int verbose)
{
    log_verbose = verbose;
}

/*
 * Function    : log_write
 * Description : Formats one line and writes it with a single stdio call,
 *               which stdio performs atomically with respect to other
 *               threads, so lines from different sessions never mix
 */
static void log_write(const char *fmt, va_list args)
{
    char line[LOG_LINE_SIZE];
    char stamp[TIME_TEXT_SIZE];
    struct tm tm;
    time_t now = time(NULL);

    if (localtime_r(&now, &tm) == NULL || strftime(stamp, sizeof stamp, TIME_FORMAT, &tm) == 0)
        stamp[0] = '\0';
    vsnprintf(line, sizeof line, fmt, args);
    fprintf(stderr, "[%s] %s\n", stamp, line);
}

/*
 * Function    : log_info
 * Description : Activity line, printed only in verbose mode
 */
void log_info(const char *fmt, ...)
{
    va_list args;

    if (!log_verbose)
        return;
    va_start(args, fmt);
    log_write(fmt, args);
    va_end(args);
}

/*
 * Function    : log_error
 * Description : Error line, always printed
 */
void log_error(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    log_write(fmt, args);
    va_end(args);
}

/*
 * Function    : log_errno_text
 * Description : Thread-safe strerror() for log messages
 * Return      : buf, holding the text for 'err'
 */
const char *log_errno_text(int err, char *buf, size_t size)
{
    if (strerror_r(err, buf, size) != 0)
        snprintf(buf, size, "error %d", err);
    return buf;
}
