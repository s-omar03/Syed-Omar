/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : strbuf.h
 *           Descriptions   : Growable text buffer with an upper size limit, filled with
 *                            printf-style calls
 *           Objective      : Build reply texts of any length safely (no fixed-size overflows)
 *           Usage          : #include "strbuf.h"
 *           Output         : None (library module)
 *
###############################################################################################*/

#ifndef STRBUF_H
#define STRBUF_H

#include <stddef.h>

#define SB_INITIAL_CAPACITY  1024   /* First allocation in bytes */

/* Text buffer. Once 'truncated' is set every further append is dropped. */
typedef struct
{
    char  *data;        /* NUL-terminated text, NULL while empty      */
    size_t length;      /* Bytes used (without the NUL)               */
    size_t capacity;    /* Bytes allocated                            */
    size_t limit;       /* Largest allowed length                     */
    int    truncated;   /* Text was dropped (limit or out of memory)  */
} StrBuf;

#if defined(__GNUC__)
#define SB_PRINTF_CHECK __attribute__((format(printf, 2, 3)))
#else
#define SB_PRINTF_CHECK
#endif

void sb_init(StrBuf *sb, size_t limit);
void sb_free(StrBuf *sb);
int  sb_printf(StrBuf *sb, const char *fmt, ...) SB_PRINTF_CHECK;
void sb_append_note(StrBuf *sb, const char *text);
const char *sb_text(const StrBuf *sb);

#endif
