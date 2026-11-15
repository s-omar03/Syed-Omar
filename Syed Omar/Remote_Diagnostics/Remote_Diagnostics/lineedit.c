/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : lineedit.c
 *           Descriptions   : Minimal line editor for the client prompt (termios raw mode):
 *                            editing keys and Up/Down arrow history navigation
 *           Objective      : Read one command line without readline/ncurses. Raw mode is on
 *                            only while a line is being typed and is always switched off
 *                            again (after each line, on errors, and through atexit()).
 *                            When stdin/stdout are not terminals, lines are read plainly.
 *           Usage          : Linked into ./client
 *           Output         : The prompt and the line being edited
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* poll(), termios */

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "lineedit.h"
#include "util.h"

#define LE_ESC_TIMEOUT_MS   50      /* Gap that separates a lone ESC from a key sequence */
#define LE_SEQ_MAX          16      /* Longest escape sequence parsed                    */
#define LE_DEFAULT_COLS     80      /* Terminal width if it cannot be queried            */
#define LE_OUT_MAX          (LE_LINE_MAX + LE_PROMPT_MAX + 64)  /* One screen update     */
#define LE_INBUF_SIZE       4096    /* Read buffer for non-terminal input               */
#define LE_WAIT_FOREVER     (-1)
#define DECIMAL_BASE        10

#define KEY_CTRL(c)         ((c) & 0x1f)
#define KEY_ESC             27
#define KEY_BACKSPACE       127
#define ASCII_FIRST         0x20
#define ASCII_LAST          0x7e
#define CSI_FINAL_FIRST     0x40    /* Final byte range of a control sequence */
#define CSI_FINAL_LAST      0x7e

#define SEQ_CLEAR_EOL       "\x1b[0K"
#define SEQ_CLEAR_SCREEN    "\x1b[H\x1b[2J"

/* Keys decoded from escape sequences */
typedef enum
{
    K_NONE = 0, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_HOME, K_END, K_DELETE
} EscKey;

/* Results of read_byte() */
typedef enum
{
    RB_OK = 0, RB_TIMEOUT, RB_EOF, RB_INTERRUPTED, RB_ERROR
} ReadResult;

/* The line being edited */
typedef struct
{
    char *buf;                  /* Caller's buffer, always NUL-terminated     */
    size_t size;                /* Its size                                   */
    size_t len;                 /* Characters in the line                     */
    size_t pos;                 /* Cursor position (0..len)                   */
    const char *prompt;
    size_t plen;
    const History *hist;
    size_t hist_index;          /* == history_count() while on the new line   */
    char saved[LE_LINE_MAX];    /* New line kept while browsing the history   */
} EditState;

static struct termios g_orig_termios;          /* Terminal settings to restore   */
static int g_raw_active = 0;                    /* 1 while raw mode is on         */
static int g_interactive = 0;                   /* stdin and stdout are terminals */
static volatile sig_atomic_t *g_interrupt = NULL;
static char g_inbuf[LE_INBUF_SIZE];             /* Plain mode input buffer        */
static size_t g_in_pos = 0;
static size_t g_in_len = 0;
static int g_in_eof = 0;

static int read_plain(const char *prompt, char *buf, size_t size);

/*
 * Function    : interrupted
 * Description : Tells whether the client's interrupt flag is set
 */
static int interrupted(void)
{
    return g_interrupt != NULL && *g_interrupt != 0;
}

/*
 * Function    : le_init
 * Description : Decides between the editor (stdin and stdout are terminals)
 *               and plain reading, remembers the terminal settings and
 *               registers le_restore() with atexit()
 */
void le_init(volatile sig_atomic_t *interrupt_flag)
{
    g_interrupt = interrupt_flag;
    if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)
        && tcgetattr(STDIN_FILENO, &g_orig_termios) == 0)
    {
        g_interactive = 1;
        atexit(le_restore);
    }
}

/*
 * Function    : le_is_interactive
 * Description : 1 if the line editor is used, 0 for plain line reading
 */
int le_is_interactive(void)
{
    return g_interactive;
}

/*
 * Function    : le_restore
 * Description : Puts the terminal back into its original mode (safe to call
 *               any number of times)
 */
void le_restore(void)
{
    if (g_raw_active)
    {
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
        g_raw_active = 0;
    }
}

/*
 * Function    : enable_raw
 * Description : Raw mode: no echo, no line buffering, no signal keys (Ctrl+C
 *               and Ctrl+D arrive as bytes), no CR->NL translation. Output
 *               processing stays on so "\n" still starts a new line.
 *               TCSANOW keeps keys typed ahead.
 * Return      : 0 on success, -1 on error
 */
static int enable_raw(void)
{
    struct termios raw = g_orig_termios;

    raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
        return -1;
    g_raw_active = 1;
    return 0;
}

/*
 * Function    : write_text
 * Description : Writes a string directly to the terminal
 */
static void write_text(const char *text)
{
    (void)util_write_all(STDOUT_FILENO, text, strlen(text));
}

/*
 * Function    : terminal_columns
 * Description : Width of the terminal in characters
 */
static size_t terminal_columns(void)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
    return LE_DEFAULT_COLS;
}

/*
 * Function    : refresh_line
 * Description : Redraws prompt and line on the current terminal row. A line
 *               wider than the terminal scrolls sideways so the cursor
 *               always stays visible.
 */
static void refresh_line(const EditState *st)
{
    char out[LE_OUT_MAX];
    size_t cols = terminal_columns(), avail, start = 0, show, n = 0;
    int written;

    if (cols < st->plen + 2)
        cols = st->plen + 2;
    avail = cols - st->plen - 1;            /* keep the last column free */
    if (st->pos > avail)
        start = st->pos - avail;
    show = st->len - start;
    if (show > avail)
        show = avail;

    out[n++] = '\r';
    memcpy(out + n, st->prompt, st->plen);
    n += st->plen;
    memcpy(out + n, st->buf + start, show);
    n += show;
    memcpy(out + n, SEQ_CLEAR_EOL, sizeof SEQ_CLEAR_EOL - 1);
    n += sizeof SEQ_CLEAR_EOL - 1;
    written = snprintf(out + n, sizeof out - n, "\r\x1b[%zuC", st->plen + st->pos - start);
    if (written > 0 && (size_t)written < sizeof out - n)
        n += (size_t)written;
    (void)util_write_all(STDOUT_FILENO, out, n);
}

/*
 * Function    : read_byte
 * Description : Reads one byte from stdin, waiting at most timeout_ms
 *               (LE_WAIT_FOREVER = no limit)
 * Return      : ReadResult
 */
static int read_byte(unsigned char *c, int timeout_ms)
{
    for (;;)
    {
        ssize_t n;

        if (timeout_ms != LE_WAIT_FOREVER)
        {
            struct pollfd pfd;
            int rc;

            pfd.fd = STDIN_FILENO;
            pfd.events = POLLIN;
            pfd.revents = 0;
            rc = poll(&pfd, 1, timeout_ms);
            if (rc == 0)
                return RB_TIMEOUT;
            if (rc < 0)
            {
                if (errno != EINTR)
                    return RB_ERROR;
                if (interrupted())
                    return RB_INTERRUPTED;
                continue;
            }
        }
        n = read(STDIN_FILENO, c, 1);
        if (n == 1)
            return RB_OK;
        if (n == 0)
            return RB_EOF;
        if (errno == EINTR)
        {
            if (interrupted())
                return RB_INTERRUPTED;
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            struct pollfd pfd;

            pfd.fd = STDIN_FILENO;
            pfd.events = POLLIN;
            pfd.revents = 0;
            (void)poll(&pfd, 1, LE_WAIT_FOREVER);
            continue;
        }
        return RB_ERROR;
    }
}

/*
 * Function    : final_key
 * Description : Maps the last byte (and parameters) of an escape sequence
 *               to a key: ESC[A..D arrows, ESC[H/F and ESC[1~/4~/7~/8~
 *               Home/End, ESC[3~ Delete. Modifiers (ESC[1;5C) are ignored.
 */
static EscKey final_key(unsigned char final, const char *params)
{
    long code;

    switch (final)
    {
    case 'A': return K_UP;
    case 'B': return K_DOWN;
    case 'C': return K_RIGHT;
    case 'D': return K_LEFT;
    case 'H': return K_HOME;
    case 'F': return K_END;
    case '~':
        code = strtol(params, NULL, DECIMAL_BASE);
        if (code == 1 || code == 7)
            return K_HOME;
        if (code == 4 || code == 8)
            return K_END;
        if (code == 3)
            return K_DELETE;
        return K_NONE;
    default:
        return K_NONE;
    }
}

/*
 * Function    : read_escape
 * Description : Reads the rest of an escape sequence after ESC. A lone ESC
 *               (nothing follows within LE_ESC_TIMEOUT_MS) and unknown
 *               sequences give K_NONE.
 * Return      : RB_OK (with *key set), RB_EOF, RB_INTERRUPTED or RB_ERROR
 */
static int read_escape(EscKey *key)
{
    char params[LE_SEQ_MAX + 1];
    size_t n = 0, total = 0;
    unsigned char c;
    int rc;

    *key = K_NONE;
    rc = read_byte(&c, LE_ESC_TIMEOUT_MS);
    if (rc != RB_OK)
        return rc == RB_TIMEOUT ? RB_OK : rc;
    if (c == 'O')                           /* ESC O A: application cursor keys */
    {
        rc = read_byte(&c, LE_ESC_TIMEOUT_MS);
        if (rc != RB_OK)
            return rc == RB_TIMEOUT ? RB_OK : rc;
        *key = final_key(c, "");
        return RB_OK;
    }
    if (c != '[')
        return RB_OK;
    for (;;)
    {
        rc = read_byte(&c, LE_ESC_TIMEOUT_MS);
        if (rc != RB_OK)
            return rc == RB_TIMEOUT ? RB_OK : rc;
        if (c >= CSI_FINAL_FIRST && c <= CSI_FINAL_LAST)
            break;
        if (++total > LE_SEQ_MAX)
            return RB_OK;                   /* not a sequence we know */
        params[n++] = (char)c;
    }
    params[n] = '\0';
    *key = final_key(c, params);
    return RB_OK;
}

/*
 * Function    : insert_char
 * Description : Inserts a character at the cursor (beeps when full)
 */
static void insert_char(EditState *st, char c)
{
    if (st->len + 1 >= st->size)
    {
        write_text("\a");
        return;
    }
    memmove(st->buf + st->pos + 1, st->buf + st->pos, st->len - st->pos);
    st->buf[st->pos] = c;
    st->len++;
    st->pos++;
    st->buf[st->len] = '\0';
}

/*
 * Function    : delete_before
 * Description : Backspace: deletes the character left of the cursor
 */
static void delete_before(EditState *st)
{
    if (st->pos == 0)
        return;
    memmove(st->buf + st->pos - 1, st->buf + st->pos, st->len - st->pos + 1);
    st->pos--;
    st->len--;
}

/*
 * Function    : delete_at
 * Description : Delete: deletes the character under the cursor
 */
static void delete_at(EditState *st)
{
    if (st->pos >= st->len)
        return;
    memmove(st->buf + st->pos, st->buf + st->pos + 1, st->len - st->pos);
    st->len--;
}

/*
 * Function    : set_line
 * Description : Replaces the line with 'text' and puts the cursor at the end
 */
static void set_line(EditState *st, const char *text)
{
    size_t n = strlen(text);

    if (n >= st->size)
        n = st->size - 1;
    memcpy(st->buf, text, n);
    st->buf[n] = '\0';
    st->len = n;
    st->pos = n;
}

/*
 * Function    : history_step
 * Description : Up (older = 1) / Down (older = 0) through the history. The
 *               line typed before pressing Up is kept and comes back after
 *               Down past the newest command.
 */
static void history_step(EditState *st, int older)
{
    size_t count = history_count(st->hist);

    if (older)
    {
        if (st->hist_index == 0)
        {
            write_text("\a");
            return;
        }
        if (st->hist_index == count)
        {
            memcpy(st->saved, st->buf, st->len + 1);
        }
        st->hist_index--;
        set_line(st, history_get(st->hist, st->hist_index));
    }
    else
    {
        if (st->hist_index >= count)
        {
            write_text("\a");
            return;
        }
        st->hist_index++;
        set_line(st, st->hist_index == count ? st->saved : history_get(st->hist, st->hist_index));
    }
}

/*
 * Function    : apply_escape_key
 * Description : Carries out an arrow / Home / End / Delete key
 */
static void apply_escape_key(EditState *st, EscKey key)
{
    switch (key)
    {
    case K_UP:     history_step(st, 1);                     break;
    case K_DOWN:   history_step(st, 0);                     break;
    case K_LEFT:   if (st->pos > 0) st->pos--;              break;
    case K_RIGHT:  if (st->pos < st->len) st->pos++;        break;
    case K_HOME:   st->pos = 0;                             break;
    case K_END:    st->pos = st->len;                       break;
    case K_DELETE: delete_at(st);                           break;
    default:                                                break;
    }
}

/*
 * Function    : handle_key
 * Description : Carries out one key press
 * Return      : 1 when the line is finished (*result set), 0 to continue
 */
static int handle_key(EditState *st, unsigned char c, int *result)
{
    EscKey key;
    int rc;

    switch (c)
    {
    case '\r':
    case '\n':
        *result = LE_OK;
        return 1;
    case KEY_CTRL('C'):                     /* cancel the line, like a shell */
        write_text("^C");
        st->buf[0] = '\0';
        st->len = st->pos = 0;
        *result = LE_OK;
        return 1;
    case KEY_CTRL('D'):                     /* EOF on an empty line, else Delete */
        if (st->len == 0)
        {
            *result = LE_EOF;
            return 1;
        }
        delete_at(st);
        return 0;
    case KEY_BACKSPACE:
    case KEY_CTRL('H'):
        delete_before(st);
        return 0;
    case KEY_CTRL('A'): st->pos = 0;                            return 0;
    case KEY_CTRL('E'): st->pos = st->len;                      return 0;
    case KEY_CTRL('B'): apply_escape_key(st, K_LEFT);           return 0;
    case KEY_CTRL('F'): apply_escape_key(st, K_RIGHT);          return 0;
    case KEY_CTRL('P'): history_step(st, 1);                    return 0;
    case KEY_CTRL('N'): history_step(st, 0);                    return 0;
    case KEY_CTRL('K'):
        st->buf[st->pos] = '\0';
        st->len = st->pos;
        return 0;
    case KEY_CTRL('U'):
        memmove(st->buf, st->buf + st->pos, st->len - st->pos + 1);
        st->len -= st->pos;
        st->pos = 0;
        return 0;
    case KEY_CTRL('L'):
        write_text(SEQ_CLEAR_SCREEN);
        return 0;
    case KEY_ESC:
        rc = read_escape(&key);
        if (rc != RB_OK)
        {
            *result = rc == RB_EOF ? LE_EOF : rc == RB_INTERRUPTED ? LE_INTERRUPTED : LE_ERROR;
            return 1;
        }
        apply_escape_key(st, key);
        return 0;
    default:
        if (c >= ASCII_FIRST && c <= ASCII_LAST)
            insert_char(st, (char)c);
        return 0;                           /* other control keys and non-ASCII are ignored */
    }
}

/*
 * Function    : read_raw
 * Description : Reads a line with the editor
 * Return      : LeResult
 */
static int read_raw(const char *prompt, char *buf, size_t size, const History *hist)
{
    EditState st;
    int result = LE_ERROR;

    fflush(stdout);
    if (enable_raw() != 0)
        return read_plain(prompt, buf, size);
    st.buf = buf;
    st.size = size;
    st.len = 0;
    st.pos = 0;
    st.prompt = prompt;
    st.plen = strlen(prompt);
    st.hist = hist;
    st.hist_index = history_count(hist);
    st.saved[0] = '\0';
    buf[0] = '\0';

    refresh_line(&st);
    for (;;)
    {
        unsigned char c;
        int rc = read_byte(&c, LE_WAIT_FOREVER);

        if (rc != RB_OK)
        {
            result = rc == RB_EOF ? LE_EOF : rc == RB_INTERRUPTED ? LE_INTERRUPTED : LE_ERROR;
            break;
        }
        if (handle_key(&st, c, &result))
            break;
        refresh_line(&st);
    }
    write_text("\n");
    le_restore();
    return result;
}

/*
 * Function    : read_plain
 * Description : Reads a line when the input is not a terminal. The prompt
 *               and the line are echoed so a scripted session reads like
 *               an interactive one. A line longer than the buffer is
 *               consumed completely and reported as LE_TOO_LONG.
 * Return      : LeResult
 */
static int read_plain(const char *prompt, char *buf, size_t size)
{
    size_t len = 0, i;
    int too_long = 0;

    fputs(prompt, stdout);
    fflush(stdout);
    for (;;)
    {
        char c;

        if (g_in_pos == g_in_len)
        {
            ssize_t n;

            if (g_in_eof)
            {
                if (len == 0 && !too_long)
                {
                    fputc('\n', stdout);
                    return LE_EOF;
                }
                break;                      /* last line without '\n' */
            }
            n = read(STDIN_FILENO, g_inbuf, sizeof g_inbuf);
            if (n > 0)
            {
                g_in_len = (size_t)n;
                g_in_pos = 0;
            }
            else if (n == 0)
                g_in_eof = 1;
            else if (errno == EINTR)
            {
                if (interrupted())
                {
                    fputc('\n', stdout);
                    return LE_INTERRUPTED;
                }
            }
            else
                return LE_ERROR;
            continue;
        }
        c = g_inbuf[g_in_pos++];
        if (c == '\n')
            break;
        if (len + 1 < size)
            buf[len++] = c;
        else
            too_long = 1;
    }
    if (len > 0 && buf[len - 1] == '\r')
        len--;
    buf[len] = '\0';

    for (i = 0; i < len; i++)
    {
        unsigned char c = (unsigned char)buf[i];

        fputc(c >= ASCII_FIRST && c <= ASCII_LAST ? c : '?', stdout);
    }
    fputs(too_long ? "...\n" : "\n", stdout);
    fflush(stdout);
    return too_long ? LE_TOO_LONG : LE_OK;
}

/*
 * Function    : le_read_line
 * Description : Shows the prompt and reads one line into buf
 * Return      : LeResult
 */
int le_read_line(const char *prompt, char *buf, size_t size, const History *hist)
{
    if (size == 0)
        return LE_ERROR;
    if (size > LE_LINE_MAX)
        size = LE_LINE_MAX;
    buf[0] = '\0';
    if (g_interactive && strlen(prompt) < LE_PROMPT_MAX)
        return read_raw(prompt, buf, size, hist);
    return read_plain(prompt, buf, size);
}
