/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : client_ui.c
 *           Descriptions   : Line reader on top of read(), terminal echo control for password
 *                            input, printing that neutralises control characters, and the
 *                            "is a prompt on screen" flag used to redraw prompts
 *           Objective      : Let the single-threaded client read the keyboard (or a pipe with
 *                            many lines at once) and the socket in one poll() loop, and never
 *                            let a peer send escape sequences to the user's terminal
 *           Usage          : Used by client.c through client_ui.h
 *           Output         : Text on stdout
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "client_ui.h"

#define REPLACEMENT_CHAR    '?'
#define ASCII_FIRST_PRINT   0x20
#define ASCII_LAST_PRINT    0x7E

/* UTF-8: lead byte ranges of 2, 3 and 4 byte sequences, continuation bytes */
#define UTF8_LEAD2_FIRST    0xC2        /* 0xC0 and 0xC1 would be overlong */
#define UTF8_LEAD2_LAST     0xDF
#define UTF8_LEAD3_FIRST    0xE0
#define UTF8_LEAD3_LAST     0xEF
#define UTF8_LEAD4_FIRST    0xF0
#define UTF8_LEAD4_LAST     0xF4
#define UTF8_LEAD2_BITS     0x1Fu
#define UTF8_LEAD3_BITS     0x0Fu
#define UTF8_LEAD4_BITS     0x07u
#define UTF8_CONT_MASK      0xC0u
#define UTF8_CONT_TAG       0x80u
#define UTF8_CONT_BITS      0x3Fu
#define UTF8_CONT_SHIFT     6
#define UTF8_MIN_3BYTE      0x800       /* smaller values are overlong      */
#define UTF8_MIN_4BYTE      0x10000
#define UNICODE_FIRST_SAFE  0xA0        /* U+0080 - U+009F are C1 controls */
#define UNICODE_MAX         0x10FFFF
#define SURROGATE_FIRST     0xD800
#define SURROGATE_LAST      0xDFFF

static struct termios saved_termios;    /* terminal settings before ui_echo_off() */
static int echo_fd = -1;                /* descriptor whose echo is switched off   */
static int prompt_visible = 0;          /* a prompt is waiting for the user        */

/*
 * Function    : line_reader_init
 * Description : Empties a line reader
 */
void line_reader_init(LineReader *reader)
{
    memset(reader, 0, sizeof *reader);
}

/*
 * Function    : line_reader_fill
 * Description : Performs one read() into the free part of the buffer. Call
 *               it only when poll() reports the descriptor readable, so it
 *               never blocks.
 * Return      : 1 if data was read (or the call should simply be retried),
 *               0 at end of input, -1 on a read error (also treated as EOF)
 */
int line_reader_fill(LineReader *reader, int fd)
{
    size_t space = sizeof reader->buf - reader->len;
    ssize_t n;

    if (reader->eof)
        return 0;
    if (space == 0)
        return 1;               /* line_reader_next() empties a full buffer */
    n = read(fd, reader->buf + reader->len, space);
    if (n > 0)
    {
        reader->len += (size_t)n;
        return 1;
    }
    if (n < 0 && (errno == EINTR || errno == EAGAIN))
        return 1;
    reader->eof = 1;
    return (n == 0) ? 0 : -1;
}

/*
 * Function    : take_bytes
 * Description : Removes the first 'count' bytes from the buffer and zeroes
 *               the freed space, so no old input (passwords) stays behind
 */
static void take_bytes(LineReader *reader, size_t count)
{
    memmove(reader->buf, reader->buf + count, reader->len - count);
    reader->len -= count;
    memset(reader->buf + reader->len, 0, count);
}

/*
 * Function    : line_reader_next
 * Description : Returns the next complete line without its newline (and
 *               without a trailing carriage return). At end of input a last
 *               line without newline is returned too. A line longer than
 *               UI_LINE_MAX is discarded and reported once, when it ends.
 *               'size' must be at least UI_LINE_BUFFER_SIZE.
 * Return      : LINE_READY, LINE_TOO_LONG, LINE_EOF or LINE_NONE
 */
LineStatus line_reader_next(LineReader *reader, char *line, size_t size)
{
    char *newline;
    size_t len;

    if (reader->discarding)
    {
        newline = memchr(reader->buf, '\n', reader->len);
        if (newline != NULL)
        {
            take_bytes(reader, (size_t)(newline - reader->buf) + 1);
            reader->discarding = 0;
            return LINE_TOO_LONG;
        }
        take_bytes(reader, reader->len);
        if (!reader->eof)
            return LINE_NONE;
        reader->discarding = 0;
        return LINE_TOO_LONG;
    }

    newline = memchr(reader->buf, '\n', reader->len);
    if (newline == NULL)
    {
        if (reader->len == sizeof reader->buf)
        {
            reader->discarding = 1;     /* Full buffer and still no newline */
            take_bytes(reader, reader->len);
            return reader->eof ? LINE_TOO_LONG : LINE_NONE;
        }
        if (!reader->eof)
            return LINE_NONE;
        if (reader->len == 0)
            return LINE_EOF;
        len = reader->len;              /* Last line without a newline */
    }
    else
    {
        len = (size_t)(newline - reader->buf);
    }

    if (len >= size)
        len = size - 1;
    memcpy(line, reader->buf, len);
    line[len] = '\0';
    take_bytes(reader, (newline != NULL) ? (size_t)(newline - reader->buf) + 1 : reader->len);
    if (len > 0 && line[len - 1] == '\r')
        line[len - 1] = '\0';
    return LINE_READY;
}

/*
 * Function    : ui_echo_off
 * Description : Stops the terminal from echoing typed characters (for the
 *               password). Does nothing when fd is not a terminal.
 * Return      : 0 on success or when not a terminal, -1 on error
 */
int ui_echo_off(int fd)
{
    struct termios quiet;

    if (echo_fd >= 0 || !isatty(fd))
        return 0;
    if (tcgetattr(fd, &saved_termios) != 0)
        return -1;
    quiet = saved_termios;
    quiet.c_lflag &= ~(tcflag_t)ECHO;
    if (tcsetattr(fd, TCSANOW, &quiet) != 0)
        return -1;
    echo_fd = fd;
    return 0;
}

/*
 * Function    : ui_echo_restore
 * Description : Restores the terminal settings saved by ui_echo_off().
 *               Safe to call at any time and more than once.
 */
void ui_echo_restore(void)
{
    if (echo_fd < 0)
        return;
    tcsetattr(echo_fd, TCSANOW, &saved_termios);
    echo_fd = -1;
}

/*
 * Function    : utf8_safe_length
 * Description : Length of a well-formed UTF-8 sequence at s that encodes a
 *               printable code point (not a C1 control, surrogate or
 *               overlong form)
 * Return      : 2 to 4, or 0 if the bytes must not be printed
 */
static size_t utf8_safe_length(const unsigned char *s)
{
    unsigned long code;
    size_t len, i;

    if (s[0] >= UTF8_LEAD2_FIRST && s[0] <= UTF8_LEAD2_LAST)
    {
        len = 2;
        code = s[0] & UTF8_LEAD2_BITS;
    }
    else if (s[0] >= UTF8_LEAD3_FIRST && s[0] <= UTF8_LEAD3_LAST)
    {
        len = 3;
        code = s[0] & UTF8_LEAD3_BITS;
    }
    else if (s[0] >= UTF8_LEAD4_FIRST && s[0] <= UTF8_LEAD4_LAST)
    {
        len = 4;
        code = s[0] & UTF8_LEAD4_BITS;
    }
    else
    {
        return 0;
    }
    for (i = 1; i < len; i++)
    {
        if ((s[i] & UTF8_CONT_MASK) != UTF8_CONT_TAG)   /* Also stops at the NUL */
            return 0;
        code = (code << UTF8_CONT_SHIFT) | (s[i] & UTF8_CONT_BITS);
    }
    if (code < UNICODE_FIRST_SAFE || code > UNICODE_MAX
        || (code >= SURROGATE_FIRST && code <= SURROGATE_LAST)
        || (len == 3 && code < UTF8_MIN_3BYTE) || (len == 4 && code < UTF8_MIN_4BYTE))
        return 0;
    return len;
}

/*
 * Function    : ui_print_safe
 * Description : Prints text received from the network. Printable ASCII and
 *               well-formed UTF-8 are printed as they are; every control
 *               character (including ESC, so no terminal escape sequences)
 *               and every invalid byte is printed as '?'.
 */
void ui_print_safe(const char *text)
{
    const unsigned char *s = (const unsigned char *)text;

    while (*s != '\0')
    {
        size_t len;

        if (*s >= ASCII_FIRST_PRINT && *s <= ASCII_LAST_PRINT)
        {
            putchar(*s++);
            continue;
        }
        len = utf8_safe_length(s);
        if (len == 0)
        {
            putchar(REPLACEMENT_CHAR);
            s++;
            continue;
        }
        fwrite(s, 1, len, stdout);
        s += len;
    }
}

/*
 * Function    : ui_show_prompt
 * Description : Prints a prompt that waits for input on the same line
 */
void ui_show_prompt(const char *prompt)
{
    fputs(prompt, stdout);
    fflush(stdout);
    prompt_visible = 1;
}

/*
 * Function    : ui_prompt_visible
 * Description : True while a prompt printed by ui_show_prompt() is waiting
 */
int ui_prompt_visible(void)
{
    return prompt_visible;
}

/*
 * Function    : ui_prompt_consumed
 * Description : Records that the user has answered the prompt
 */
void ui_prompt_consumed(void)
{
    prompt_visible = 0;
}
