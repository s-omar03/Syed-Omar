/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : strbuf.c
 *           Descriptions   : Growable text buffer with an upper size limit, filled with
 *                            printf-style calls
 *           Objective      : Build reply texts of any length safely (no fixed-size overflows)
 *           Usage          : Linked into ./server
 *           Output         : None (library module)
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "strbuf.h"

#define SB_GROWTH_FACTOR  2

/*
 * Function    : sb_init
 * Description : Initializes an empty buffer whose text may grow to 'limit' bytes
 */
void sb_init(StrBuf *sb, size_t limit)
{
    sb->data = NULL;
    sb->length = 0;
    sb->capacity = 0;
    sb->limit = limit;
    sb->truncated = 0;
}

/*
 * Function    : sb_free
 * Description : Releases the memory of the buffer and empties it
 */
void sb_free(StrBuf *sb)
{
    free(sb->data);
    sb_init(sb, sb->limit);
}

/*
 * Function    : sb_reserve
 * Description : Makes room for 'needed' bytes of text plus the NUL
 * Return      : 0 on success, -1 if out of memory
 */
static int sb_reserve(StrBuf *sb, size_t needed)
{
    size_t new_capacity;
    char *p;

    if (needed < sb->capacity)
        return 0;
    new_capacity = sb->capacity == 0 ? SB_INITIAL_CAPACITY : sb->capacity;
    while (new_capacity <= needed)
    {
        if (new_capacity > ((size_t)-1) / SB_GROWTH_FACTOR)
            return -1;
        new_capacity *= SB_GROWTH_FACTOR;
    }
    p = realloc(sb->data, new_capacity);
    if (p == NULL)
        return -1;
    if (sb->data == NULL)
        p[0] = '\0';
    sb->data = p;
    sb->capacity = new_capacity;
    return 0;
}

/*
 * Function    : sb_printf
 * Description : Appends formatted text. If the text would make the buffer
 *               longer than its limit (or memory runs out), nothing is
 *               appended and the buffer is marked as truncated; later appends
 *               are dropped too, so the text never has holes in it.
 * Return      : 0 if appended, -1 if dropped
 */
int sb_printf(StrBuf *sb, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (sb->truncated)
        return -1;

    va_start(ap, fmt);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0 || sb->length > sb->limit || (size_t)n > sb->limit - sb->length)
    {
        sb->truncated = 1;
        return -1;
    }
    if (sb_reserve(sb, sb->length + (size_t)n) != 0)
    {
        sb->truncated = 1;
        return -1;
    }
    va_start(ap, fmt);
    vsnprintf(sb->data + sb->length, sb->capacity - sb->length, fmt, ap);
    va_end(ap);
    sb->length += (size_t)n;
    return 0;
}

/*
 * Function    : sb_append_note
 * Description : Appends text even past the limit (used for the single
 *               "output truncated" note). Fails silently if out of memory.
 */
void sb_append_note(StrBuf *sb, const char *text)
{
    size_t n = strlen(text);

    if (sb_reserve(sb, sb->length + n) != 0)
        return;
    memcpy(sb->data + sb->length, text, n + 1);
    sb->length += n;
}

/*
 * Function    : sb_text
 * Description : The text of the buffer ("" while empty)
 */
const char *sb_text(const StrBuf *sb)
{
    return sb->data != NULL ? sb->data : "";
}
