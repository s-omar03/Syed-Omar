/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : procinfo.c
 *           Descriptions   : Readers for the /proc file system: process lookup by name,
 *                            /proc/<pid>/stat and status fields, open file descriptors
 *           Objective      : Collect process information without running any shell command.
 *                            Processes may exit at any moment, so every read can fail;
 *                            callers treat a failed read as "the process is gone".
 *           Usage          : Linked into ./server
 *           Output         : None (library module)
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* O_CLOEXEC, readlink(), opendir() */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "procinfo.h"

#define DECIMAL_BASE     10
#define OCTAL_BASE       8
#define PID_TEXT_MAX     10         /* Digits of the largest PID              */
#define STAT_FIELD_COUNT 6          /* Fields parsed from /proc/<pid>/stat    */
#define FDINFO_FLAGS_KEY "flags"
#define MEMINFO_PATH     PROC_ROOT "/meminfo"
#define UPTIME_PATH      PROC_ROOT "/uptime"
#define LIST_GROWTH      2

/*
 * Function    : proc_read_text
 * Description : Reads a whole (small) file into buf and NUL-terminates it.
 *               At most size-1 bytes are read. *length (if not NULL) gets the
 *               number of bytes, which matters for files containing NULs.
 * Return      : 0 on success, -1 on error (errno set)
 */
int proc_read_text(const char *path, char *buf, size_t size, size_t *length)
{
    size_t used = 0;
    int fd, saved_errno;

    if (size == 0)
    {
        errno = EINVAL;
        return -1;
    }
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    while (used < size - 1)
    {
        ssize_t n = read(fd, buf + used, size - 1 - used);

        if (n > 0)
        {
            used += (size_t)n;
            continue;
        }
        if (n == 0)
            break;
        if (errno == EINTR)
            continue;
        saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    close(fd);
    buf[used] = '\0';
    if (length != NULL)
        *length = used;
    return 0;
}

/*
 * Function    : parse_pid
 * Description : Converts a /proc directory name to a PID; only names made of
 *               digits are process directories
 * Return      : 0 and *pid on success, -1 if the name is not a PID
 */
static int parse_pid(const char *text, pid_t *pid)
{
    size_t i, len = strlen(text);
    long value;

    if (len == 0 || len > PID_TEXT_MAX)
        return -1;
    for (i = 0; i < len; i++)
    {
        if (text[i] < '0' || text[i] > '9')
            return -1;
    }
    errno = 0;
    value = strtol(text, NULL, DECIMAL_BASE);
    if (errno != 0 || value <= 0 || value > INT_MAX)
        return -1;
    *pid = (pid_t)value;
    return 0;
}

/*
 * Function    : proc_read_stat
 * Description : Reads /proc/<pid>/stat. The name is between the first '('
 *               and the LAST ')' because the name itself may contain ')' and
 *               spaces; the numeric fields follow the last ')'.
 * Return      : 0 on success, -1 if the process is gone or the file is odd
 */
int proc_read_stat(pid_t pid, ProcStat *st)
{
    char path[PROC_PATH_MAX], buf[PROC_STAT_BUF];
    const char *open_paren, *close_paren;
    size_t name_len;

    snprintf(path, sizeof path, PROC_ROOT "/%ld/stat", (long)pid);
    if (proc_read_text(path, buf, sizeof buf, NULL) != 0)
        return -1;
    open_paren = strchr(buf, '(');
    close_paren = strrchr(buf, ')');
    if (open_paren == NULL || close_paren == NULL || close_paren < open_paren)
    {
        errno = EINVAL;
        return -1;
    }
    name_len = (size_t)(close_paren - open_paren - 1);
    if (name_len >= sizeof st->comm)
        name_len = sizeof st->comm - 1;
    memcpy(st->comm, open_paren + 1, name_len);
    st->comm[name_len] = '\0';

    /* Fields 3..22: state ppid pgrp session tty_nr tpgid flags minflt cminflt
     * majflt cmajflt utime stime cutime cstime priority nice num_threads
     * itrealvalue starttime */
    if (sscanf(close_paren + 1,
               " %c %ld %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu %*d %*d %*d %*d %ld %*d %llu",
               &st->state, &st->ppid, &st->utime, &st->stime, &st->num_threads,
               &st->start_ticks) != STAT_FIELD_COUNT)
    {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

/*
 * Function    : argv0_matches
 * Description : Tells whether the file name part of the program path the
 *               process was started with (argv[0] in /proc/<pid>/cmdline)
 *               equals 'name'
 * Return      : 1 on a match, 0 otherwise
 */
static int argv0_matches(pid_t pid, const char *name)
{
    char path[PROC_PATH_MAX], buf[PROC_CMDLINE_BUF];
    const char *base;
    size_t length;

    snprintf(path, sizeof path, PROC_ROOT "/%ld/cmdline", (long)pid);
    if (proc_read_text(path, buf, sizeof buf, &length) != 0 || length == 0)
        return 0;
    /* buf is NUL-terminated after 'length' bytes, so argv[0] ends at the first NUL */
    base = strrchr(buf, '/');
    base = base != NULL ? base + 1 : buf;
    return strcmp(base, name) == 0;
}

/*
 * Function    : name_matches
 * Description : Matching rule. A process matches when its kernel name (comm,
 *               what ps/top/pgrep show) equals 'name'. The kernel keeps only
 *               15 characters, so a longer 'name' matches when comm equals
 *               its first 15 characters AND argv[0] confirms the full name.
 * Return      : 1 on a match, 0 otherwise
 */
static int name_matches(pid_t pid, const char *comm, const char *name)
{
    size_t keep = PROC_TASK_COMM_LEN - 1;

    if (strcmp(comm, name) == 0)
        return 1;
    if (strlen(name) > keep && strlen(comm) == keep && strncmp(comm, name, keep) == 0)
        return argv0_matches(pid, name);
    return 0;
}

/*
 * Function    : compare_entries
 * Description : qsort() callback ordering processes by PID
 */
static int compare_entries(const void *a, const void *b)
{
    const ProcEntry *x = a, *y = b;

    return (x->pid > y->pid) - (x->pid < y->pid);
}

/*
 * Function    : list_append
 * Description : Adds one process to the list, growing it when needed
 * Return      : 0 on success, -1 if out of memory
 */
static int list_append(ProcList *list, pid_t pid, const ProcStat *st)
{
    ProcEntry *e;

    if (list->count == list->capacity)
    {
        size_t new_capacity = list->capacity == 0 ? PROC_LIST_INITIAL
                                                  : list->capacity * LIST_GROWTH;
        ProcEntry *p = realloc(list->items, new_capacity * sizeof *p);

        if (p == NULL)
            return -1;
        list->items = p;
        list->capacity = new_capacity;
    }
    e = &list->items[list->count++];
    e->pid = pid;
    memcpy(e->comm, st->comm, sizeof e->comm);
    e->start_ticks = st->start_ticks;
    return 0;
}

/*
 * Function    : proc_find_by_name
 * Description : Scans /proc for living processes (zombies are skipped) whose
 *               name matches. Processes that exit during the scan are simply
 *               not found.
 * Return      : 0 on success (list->count may be 0), -1 on error (errno set)
 */
int proc_find_by_name(const char *name, ProcList *list)
{
    DIR *dir;
    int saved_errno;

    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
    list->truncated = 0;

    dir = opendir(PROC_ROOT);
    if (dir == NULL)
        return -1;
    for (;;)
    {
        struct dirent *de;
        ProcStat st;
        pid_t pid;

        errno = 0;
        de = readdir(dir);
        if (de == NULL)
        {
            if (errno != 0)
                goto fail;
            break;
        }
        if (parse_pid(de->d_name, &pid) != 0 || proc_read_stat(pid, &st) != 0)
            continue;
        if (st.state == 'Z' || st.state == 'X' || st.state == 'x')
            continue;
        if (!name_matches(pid, st.comm, name))
            continue;
        if (list->count >= PROC_MAX_MATCHES)
        {
            list->truncated = 1;
            continue;
        }
        if (list_append(list, pid, &st) != 0)
        {
            errno = ENOMEM;
            goto fail;
        }
    }
    closedir(dir);
    if (list->count > 1)
        qsort(list->items, list->count, sizeof list->items[0], compare_entries);
    return 0;

fail:
    saved_errno = errno;
    closedir(dir);
    proc_list_free(list);
    errno = saved_errno;
    return -1;
}

/*
 * Function    : proc_list_free
 * Description : Releases a process list
 */
void proc_list_free(ProcList *list)
{
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

/*
 * Function    : proc_is_alive
 * Description : Tells whether the process found earlier still runs. It is
 *               gone when /proc/<pid> vanished, when it is a zombie (it has
 *               exited and waits for its parent), or when the PID now belongs
 *               to a different process (other start time).
 * Return      : 1 if alive, 0 if gone
 */
int proc_is_alive(const ProcEntry *entry)
{
    ProcStat st;

    if (proc_read_stat(entry->pid, &st) != 0)
        return 0;
    if (st.state == 'Z' || st.state == 'X' || st.state == 'x')
        return 0;
    return st.start_ticks == entry->start_ticks;
}

/*
 * Function    : proc_field_kb
 * Description : Finds a "Key:   value kB" line (format of /proc/<pid>/status
 *               and /proc/meminfo) and returns the value
 * Return      : 0 on success, -1 if the key is missing
 */
int proc_field_kb(const char *text, const char *key, unsigned long long *value)
{
    size_t key_len = strlen(key);
    const char *line = text;

    while (line != NULL && *line != '\0')
    {
        if (strncmp(line, key, key_len) == 0 && line[key_len] == ':')
        {
            char *end;

            errno = 0;
            *value = strtoull(line + key_len + 1, &end, DECIMAL_BASE);
            if (errno != 0 || end == line + key_len + 1)
                return -1;
            return 0;
        }
        line = strchr(line, '\n');
        if (line != NULL)
            line++;
    }
    return -1;
}

/*
 * Function    : proc_meminfo_kb
 * Description : Reads one value (for example "MemTotal") from /proc/meminfo
 * Return      : 0 on success, -1 on error
 */
int proc_meminfo_kb(const char *key, unsigned long long *value)
{
    char buf[PROC_STATUS_BUF];

    if (proc_read_text(MEMINFO_PATH, buf, sizeof buf, NULL) != 0)
        return -1;
    return proc_field_kb(buf, key, value);
}

/*
 * Function    : proc_uptime_seconds
 * Description : Seconds since the system booted (first number of /proc/uptime)
 * Return      : 0 on success, -1 on error
 */
int proc_uptime_seconds(double *seconds)
{
    char buf[PROC_SMALL_BUF], *end;

    if (proc_read_text(UPTIME_PATH, buf, sizeof buf, NULL) != 0)
        return -1;
    errno = 0;
    *seconds = strtod(buf, &end);
    if (errno != 0 || end == buf)
        return -1;
    return 0;
}

/*
 * Function    : compare_ints
 * Description : qsort() callback for ascending descriptor numbers
 */
static int compare_ints(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;

    return (x > y) - (x < y);
}

/*
 * Function    : proc_list_fds
 * Description : Lists the open descriptor numbers of a process from
 *               /proc/<pid>/fd, sorted. The caller frees *fds.
 * Return      : 0 on success, -1 on error (errno: EACCES = not allowed,
 *               ENOENT = process gone)
 */
int proc_list_fds(pid_t pid, int **fds, size_t *count)
{
    char path[PROC_PATH_MAX];
    int *list = NULL;
    size_t used = 0, capacity = 0;
    DIR *dir;
    int saved_errno;

    *fds = NULL;
    *count = 0;
    snprintf(path, sizeof path, PROC_ROOT "/%ld/fd", (long)pid);
    dir = opendir(path);
    if (dir == NULL)
        return -1;
    for (;;)
    {
        struct dirent *de;
        pid_t number;

        errno = 0;
        de = readdir(dir);
        if (de == NULL)
        {
            if (errno != 0)
                goto fail;
            break;
        }
        if (strcmp(de->d_name, "0") == 0)
            number = 0;
        else if (parse_pid(de->d_name, &number) != 0)   /* same digit rules as a PID */
            continue;
        if (used == capacity)
        {
            size_t new_capacity = capacity == 0 ? PROC_LIST_INITIAL : capacity * LIST_GROWTH;
            int *p = realloc(list, new_capacity * sizeof *p);

            if (p == NULL)
            {
                errno = ENOMEM;
                goto fail;
            }
            list = p;
            capacity = new_capacity;
        }
        list[used++] = (int)number;
    }
    closedir(dir);
    if (used > 1)
        qsort(list, used, sizeof list[0], compare_ints);
    *fds = list;
    *count = used;
    return 0;

fail:
    saved_errno = errno;
    closedir(dir);
    free(list);
    errno = saved_errno;
    return -1;
}

/*
 * Function    : proc_fd_target
 * Description : What a descriptor refers to: readlink of /proc/<pid>/fd/<fd>
 *               (a path, "socket:[inode]", "pipe:[inode]", "anon_inode:..." ...)
 * Return      : 0 on success, -1 if the descriptor (or process) is gone
 */
int proc_fd_target(pid_t pid, int fd, char *buf, size_t size)
{
    char path[PROC_PATH_MAX];
    ssize_t n;

    if (size == 0)
        return -1;
    snprintf(path, sizeof path, PROC_ROOT "/%ld/fd/%d", (long)pid, fd);
    n = readlink(path, buf, size - 1);
    if (n < 0)
        return -1;
    buf[n] = '\0';
    return 0;
}

/*
 * Function    : proc_fd_flags
 * Description : Open flags of a descriptor (the octal "flags:" line of
 *               /proc/<pid>/fdinfo/<fd>)
 * Return      : 0 on success, -1 on error
 */
int proc_fd_flags(pid_t pid, int fd, unsigned long *flags)
{
    char path[PROC_PATH_MAX], buf[PROC_SMALL_BUF];
    size_t key_len = strlen(FDINFO_FLAGS_KEY);
    const char *line;

    snprintf(path, sizeof path, PROC_ROOT "/%ld/fdinfo/%d", (long)pid, fd);
    if (proc_read_text(path, buf, sizeof buf, NULL) != 0)
        return -1;
    for (line = buf; line != NULL && *line != '\0'; )
    {
        if (strncmp(line, FDINFO_FLAGS_KEY, key_len) == 0 && line[key_len] == ':')
        {
            char *end;

            errno = 0;
            *flags = strtoul(line + key_len + 1, &end, OCTAL_BASE);
            return (errno != 0 || end == line + key_len + 1) ? -1 : 0;
        }
        line = strchr(line, '\n');
        if (line != NULL)
            line++;
    }
    return -1;
}
