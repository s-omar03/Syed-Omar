/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : history.c
 *           Descriptions   : Command history of the client: array of at most 50 commands,
 *                            saved to .remote_history on exit and loaded on start
 *           Objective      : Support the "history" command and Up/Down arrow navigation
 *           Usage          : Linked into ./client
 *           Output         : "<n> <command>" lines for the history command
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* O_NOFOLLOW, O_CLOEXEC, fdopen() */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "history.h"

#define HISTORY_FILE_MODE  0600     /* Only the owner may read the history       */
#define ASCII_FIRST        0x20
#define ASCII_LAST         0x7e

/*
 * Function    : history_init
 * Description : Empties the history
 */
void history_init(History *h)
{
    h->count = 0;
}

/*
 * Function    : valid_line
 * Description : A history line is 1..HISTORY_LINE_MAX-1 printable ASCII
 *               characters (anything else in the file is ignored)
 * Return      : 1 if valid, 0 otherwise
 */
static int valid_line(const char *line)
{
    size_t i, len = strlen(line);

    if (len == 0 || len >= HISTORY_LINE_MAX)
        return 0;
    for (i = 0; i < len; i++)
    {
        unsigned char c = (unsigned char)line[i];

        if (c < ASCII_FIRST || c > ASCII_LAST)
            return 0;
    }
    return 1;
}

/*
 * Function    : history_add
 * Description : Appends a command. When the history is full the oldest
 *               command is dropped, so the last HISTORY_MAX are kept.
 */
void history_add(History *h, const char *line)
{
    if (!valid_line(line))
        return;
    if (h->count == HISTORY_MAX)
    {
        memmove(h->entries[0], h->entries[1], (HISTORY_MAX - 1) * sizeof h->entries[0]);
        h->count--;
    }
    memcpy(h->entries[h->count], line, strlen(line) + 1);
    h->count++;
}

/*
 * Function    : history_count
 * Description : Number of stored commands
 */
size_t history_count(const History *h)
{
    return h->count;
}

/*
 * Function    : history_get
 * Description : Command number 'index' (0 = oldest)
 * Return      : The command, or NULL if the index is out of range
 */
const char *history_get(const History *h, size_t index)
{
    return index < h->count ? h->entries[index] : NULL;
}

/*
 * Function    : history_print
 * Description : Prints "1 <oldest command>" ... "N <newest command>"
 *               (format of the spec's Fig 5.7)
 */
void history_print(const History *h, FILE *out)
{
    size_t i;

    if (h->count == 0)
    {
        fprintf(out, "History is empty\n");
        return;
    }
    for (i = 0; i < h->count; i++)
        fprintf(out, "%zu %s\n", i + 1, h->entries[i]);
}

/*
 * Function    : history_default_path
 * Description : $HOME/.remote_history, or ./.remote_history when HOME is
 *               not set to an absolute path
 * Return      : 0 on success, -1 if the path does not fit
 */
int history_default_path(char *buf, size_t size)
{
    const char *home = getenv("HOME");
    int n;

    if (home != NULL && home[0] == '/')
    {
        n = snprintf(buf, size, "%s/%s", home, HISTORY_FILE_NAME);
        if (n > 0 && (size_t)n < size)
            return 0;
    }
    n = snprintf(buf, size, "%s", HISTORY_FILE_NAME);
    return (n > 0 && (size_t)n < size) ? 0 : -1;
}

/*
 * Function    : history_load
 * Description : Loads the history file. A missing file is not an error.
 *               Lines that are too long or contain non-printable characters
 *               are skipped; only the last HISTORY_MAX lines are kept.
 * Return      : 0 on success, -1 on a read error (errno set)
 */
int history_load(History *h, const char *path)
{
    char line[HISTORY_LINE_MAX + 2];
    FILE *fp = fopen(path, "r");
    int skipping = 0, failed;

    if (fp == NULL)
        return errno == ENOENT ? 0 : -1;
    while (fgets(line, sizeof line, fp) != NULL)
    {
        size_t len = strlen(line);
        int complete = len > 0 && line[len - 1] == '\n';

        if (skipping)
        {
            skipping = !complete;           /* still inside an over-long line */
            continue;
        }
        if (!complete && !feof(fp))
        {
            skipping = 1;                   /* over-long line: ignore all of it */
            continue;
        }
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        history_add(h, line);
    }
    failed = ferror(fp);
    fclose(fp);
    if (failed)
    {
        errno = EIO;
        return -1;
    }
    return 0;
}

/*
 * Function    : history_save
 * Description : Writes the history to a new temporary file (mode 0600,
 *               created exclusively, never through a symlink) and renames
 *               it over the history file, so a crash cannot leave a half
 *               written history behind
 * Return      : 0 on success, -1 on error (errno set)
 */
int history_save(const History *h, const char *path)
{
    char tmp[HISTORY_PATH_MAX];
    FILE *fp;
    size_t i;
    int fd, n, saved_errno;

    n = snprintf(tmp, sizeof tmp, "%s.%ld.tmp", path, (long)getpid());
    if (n < 0 || (size_t)n >= sizeof tmp)
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, HISTORY_FILE_MODE);
    if (fd < 0)
        return -1;
    fp = fdopen(fd, "w");
    if (fp == NULL)
    {
        saved_errno = errno;
        close(fd);
        unlink(tmp);
        errno = saved_errno;
        return -1;
    }
    for (i = 0; i < h->count; i++)
        fprintf(fp, "%s\n", h->entries[i]);
    if (fflush(fp) != 0 || ferror(fp))
    {
        saved_errno = errno;
        fclose(fp);
        unlink(tmp);
        errno = saved_errno;
        return -1;
    }
    if (fclose(fp) != 0 || rename(tmp, path) != 0)
    {
        saved_errno = errno;
        unlink(tmp);
        errno = saved_errno;
        return -1;
    }
    return 0;
}
