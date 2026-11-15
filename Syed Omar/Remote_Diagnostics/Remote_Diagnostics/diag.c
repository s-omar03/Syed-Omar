/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : diag.c
 *           Descriptions   : Server-side commands get-mem, get-cpu-usage, get-open-fd and kill
 *           Objective      : Turn /proc data for the matching processes into reply text.
 *                            Each function writes one line (or block) per process and
 *                            returns a ReplyStatus.
 *           Usage          : Linked into ./server
 *           Output         : Reply text in a StrBuf
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* kill(), sysconf() */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "diag.h"
#include "protocol.h"
#include "util.h"

#define PERCENT            100.0
#define CENTIS_PER_SECOND  100ULL
#define CENTIS_PER_MINUTE  6000ULL
#define SECONDS_PER_MINUTE 60ULL
#define CPU_TIME_TEXT_MAX  32
#define INIT_PID           1
#define DEFAULT_CLK_TCK    100       /* Used if sysconf(_SC_CLK_TCK) fails */

/* Column layouts of the tables */
#define MEM_HEADER_FMT  "%-7s %6s %10s %10s %10s %9s  %s\n"
#define MEM_ROW_FMT     "%-7ld %6.1f %10llu %10llu %10llu %9llu  %s\n"
#define MEM_KTHREAD_FMT "%-7ld %6s %10s %10s %10s %9s  %s (kernel thread, no user memory)\n"
#define CPU_HEADER_FMT  "%-7s %6s %8s %11s %7s %5s  %s\n"
#define CPU_ROW_FMT     "%-7ld %6.1f %8.1f %11s %7ld %5c  %s\n"
#define FD_HEADER_FMT   "%-6s %-4s %s\n"
#define FD_ROW_FMT      "%-6d %-4s %s\n"

/* What happened to one process during "kill" */
typedef enum
{
    KILL_SENT,              /* signal delivered                         */
    KILL_FAILED,            /* kill() failed, errno kept                */
    KILL_ALREADY_GONE,      /* exited before the signal was sent        */
    KILL_SKIP_SELF,         /* the server process answering the request */
    KILL_SKIP_INIT          /* PID 1                                    */
} KillState;

/*
 * Function    : diag_name
 * Description : Process name made safe for printing (non-printable bytes
 *               are shown as \xNN)
 * Return      : buf
 */
const char *diag_name(const ProcEntry *entry, char *buf, size_t size)
{
    util_escape(entry->comm, strlen(entry->comm), buf, size);
    return buf;
}

/*
 * Function    : diag_status
 * Description : Overall status from the number of processes and failures
 * Return      : STATUS_OK, STATUS_PARTIAL or STATUS_FAILED
 */
int diag_status(size_t total, size_t failed)
{
    if (failed == 0)
        return STATUS_OK;
    return failed < total ? STATUS_PARTIAL : STATUS_FAILED;
}

/*
 * Function    : diag_get_mem
 * Description : get-mem: one table row per process with values from
 *               /proc/<pid>/status:
 *                 RSS  = VmRSS  (physical memory in use now)
 *                 VSZ  = VmSize (virtual address space)
 *                 PEAK = VmHWM  (highest RSS so far)
 *                 SWAP = VmSwap (memory swapped out)
 *                 %MEM = RSS as a percentage of MemTotal (/proc/meminfo)
 * Return      : ReplyStatus
 */
int diag_get_mem(const ProcList *list, StrBuf *out)
{
    unsigned long long total_kb = 0;
    int have_total = proc_meminfo_kb("MemTotal", &total_kb) == 0 && total_kb > 0;
    size_t i, failed = 0;

    sb_printf(out, MEM_HEADER_FMT, "PID", "%MEM", "RSS(kB)", "VSZ(kB)", "PEAK(kB)",
              "SWAP(kB)", "NAME");
    for (i = 0; i < list->count; i++)
    {
        const ProcEntry *e = &list->items[i];
        char path[PROC_PATH_MAX], status[PROC_STATUS_BUF], name[NAME_TEXT_MAX];
        unsigned long long rss, vsz, peak, swap;
        double percent;

        diag_name(e, name, sizeof name);
        snprintf(path, sizeof path, PROC_ROOT "/%ld/status", (long)e->pid);
        if (proc_read_text(path, status, sizeof status, NULL) != 0)
        {
            sb_printf(out, "%-7ld exited before it could be measured  %s\n", (long)e->pid, name);
            failed++;
            continue;
        }
        if (proc_field_kb(status, "VmSize", &vsz) != 0)
        {
            sb_printf(out, MEM_KTHREAD_FMT, (long)e->pid, "-", "-", "-", "-", "-", name);
            continue;
        }
        if (proc_field_kb(status, "VmRSS", &rss) != 0)
            rss = 0;
        if (proc_field_kb(status, "VmHWM", &peak) != 0)
            peak = rss;
        if (proc_field_kb(status, "VmSwap", &swap) != 0)
            swap = 0;
        percent = have_total ? (double)rss * PERCENT / (double)total_kb : 0.0;
        sb_printf(out, MEM_ROW_FMT, (long)e->pid, percent, rss, vsz, peak, swap, name);
    }
    if (have_total)
        sb_printf(out, "(%%MEM = RSS as a share of the %llu kB of RAM)\n", total_kb);
    return diag_status(list->count, failed);
}

/*
 * Function    : format_cpu_time
 * Description : Formats clock ticks as minutes:seconds.hundredths
 */
static void format_cpu_time(unsigned long long ticks, long hz, char *buf, size_t size)
{
    unsigned long long centis = ticks * CENTIS_PER_SECOND / (unsigned long long)hz;

    snprintf(buf, size, "%llu:%02llu.%02llu", centis / CENTIS_PER_MINUTE,
             (centis / CENTIS_PER_SECOND) % SECONDS_PER_MINUTE, centis % CENTIS_PER_SECOND);
}

/*
 * Function    : diag_get_cpu
 * Description : get-cpu-usage. Reads utime+stime of every process, waits
 *               CPU_SAMPLE_MS, reads them again:
 *                 %CPU    = CPU time used in the interval / interval length
 *                           (100 = one core fully busy, like top)
 *                 AVG%CPU = total CPU time / time since the process started
 *                           (what ps shows)
 *                 CPU-TIME = total user + system CPU time (min:sec.hundredths)
 * Return      : ReplyStatus
 */
int diag_get_cpu(const ProcList *list, StrBuf *out)
{
    long hz = sysconf(_SC_CLK_TCK);
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    ProcStat *before;
    unsigned char *valid;
    double elapsed, uptime = 0.0;
    long long start_ms;
    int have_uptime;
    size_t i, failed = 0;

    if (hz <= 0)
        hz = DEFAULT_CLK_TCK;
    before = calloc(list->count, sizeof *before);
    valid = calloc(list->count, sizeof *valid);
    if (before == NULL || valid == NULL)
    {
        free(before);
        free(valid);
        sb_printf(out, "The server ran out of memory\n");
        return STATUS_FAILED;
    }

    for (i = 0; i < list->count; i++)
    {
        const ProcEntry *e = &list->items[i];

        valid[i] = proc_read_stat(e->pid, &before[i]) == 0
                   && before[i].start_ticks == e->start_ticks;
    }
    start_ms = util_now_ms();
    util_sleep_ms(CPU_SAMPLE_MS);
    elapsed = (double)(util_now_ms() - start_ms) / (double)MS_PER_SECOND;
    if (elapsed <= 0.0)
        elapsed = (double)CPU_SAMPLE_MS / (double)MS_PER_SECOND;
    have_uptime = proc_uptime_seconds(&uptime) == 0;

    sb_printf(out, CPU_HEADER_FMT, "PID", "%CPU", "AVG%CPU", "CPU-TIME", "THREADS", "STATE",
              "NAME");
    for (i = 0; i < list->count; i++)
    {
        const ProcEntry *e = &list->items[i];
        char name[NAME_TEXT_MAX], cpu_time[CPU_TIME_TEXT_MAX];
        unsigned long long ticks_before, ticks_after;
        double percent, average = 0.0, lifetime;
        ProcStat after;

        diag_name(e, name, sizeof name);
        if (!valid[i] || proc_read_stat(e->pid, &after) != 0
            || after.start_ticks != e->start_ticks || after.state == 'Z' || after.state == 'X')
        {
            sb_printf(out, "%-7ld exited during the measurement  %s\n", (long)e->pid, name);
            failed++;
            continue;
        }
        ticks_before = before[i].utime + before[i].stime;
        ticks_after = after.utime + after.stime;
        percent = ticks_after >= ticks_before
                  ? (double)(ticks_after - ticks_before) / (double)hz / elapsed * PERCENT : 0.0;
        lifetime = have_uptime ? uptime - (double)after.start_ticks / (double)hz : 0.0;
        if (lifetime > 0.0)
            average = (double)ticks_after / (double)hz / lifetime * PERCENT;
        format_cpu_time(ticks_after, hz, cpu_time, sizeof cpu_time);
        sb_printf(out, CPU_ROW_FMT, (long)e->pid, percent, average, cpu_time,
                  after.num_threads, after.state, name);
    }
    sb_printf(out, "(%%CPU = share of one CPU core used during the last %.1f s; "
              "AVG%%CPU = average since the process started;\n"
              " %ld CPU core(s) online, so a multi-threaded process can reach %ld%%)\n",
              elapsed, cpus > 0 ? cpus : 1L, (cpus > 0 ? cpus : 1L) * (long)PERCENT);
    free(before);
    free(valid);
    return diag_status(list->count, failed);
}

/*
 * Function    : fd_mode
 * Description : Access mode of a descriptor from its open flags
 * Return      : "r", "w", "rw" or "?" if unknown
 */
static const char *fd_mode(pid_t pid, int fd)
{
    unsigned long flags;

    if (proc_fd_flags(pid, fd, &flags) != 0)
        return "?";
    switch (flags & (unsigned long)O_ACCMODE)
    {
    case O_RDONLY: return "r";
    case O_WRONLY: return "w";
    case O_RDWR:   return "rw";
    default:       return "?";
    }
}

/*
 * Function    : diag_get_open_fd
 * Description : get-open-fd: for each process the number of open
 *               descriptors and a table FD / MODE / TARGET (the readlink of
 *               /proc/<pid>/fd/<n>). At most FD_LIST_LIMIT rows per process.
 * Return      : ReplyStatus
 */
int diag_get_open_fd(const ProcList *list, StrBuf *out)
{
    char target[PROC_LINK_MAX], escaped[PROC_LINK_MAX];
    size_t i, failed = 0;

    for (i = 0; i < list->count; i++)
    {
        const ProcEntry *e = &list->items[i];
        char name[NAME_TEXT_MAX];
        int *fds;
        size_t n, j;

        diag_name(e, name, sizeof name);
        if (i > 0)
            sb_printf(out, "\n");
        if (proc_list_fds(e->pid, &fds, &n) != 0)
        {
            int err = errno;

            sb_printf(out, "PID %ld (%s): cannot read the open files: %s\n", (long)e->pid, name,
                      err == ENOENT ? "the process has exited" : strerror(err));
            failed++;
            continue;
        }
        sb_printf(out, "PID %ld (%s): %zu open file descriptor%s\n", (long)e->pid, name, n,
                  n == 1 ? "" : "s");
        if (n > 0)
            sb_printf(out, FD_HEADER_FMT, "FD", "MODE", "TARGET");
        for (j = 0; j < n && j < FD_LIST_LIMIT; j++)
        {
            if (proc_fd_target(e->pid, fds[j], target, sizeof target) != 0)
                continue;                   /* closed in the meantime */
            util_escape(target, strlen(target), escaped, sizeof escaped);
            sb_printf(out, FD_ROW_FMT, fds[j], fd_mode(e->pid, fds[j]), escaped);
        }
        if (n > FD_LIST_LIMIT)
            sb_printf(out, "... %zu more descriptors not shown\n", n - FD_LIST_LIMIT);
        free(fds);
    }
    return diag_status(list->count, failed);
}

/*
 * Function    : wait_for_exit
 * Description : Waits up to KILL_WAIT_MS until every signalled process has
 *               exited, checking every KILL_POLL_MS. alive[i] is cleared for
 *               each process that is gone.
 */
static void wait_for_exit(const ProcList *list, const unsigned char *state, unsigned char *alive)
{
    long long deadline = util_now_ms() + KILL_WAIT_MS;

    for (;;)
    {
        size_t i, remaining = 0;

        for (i = 0; i < list->count; i++)
        {
            if (state[i] != KILL_SENT || !alive[i])
                continue;
            if (proc_is_alive(&list->items[i]))
                remaining++;
            else
                alive[i] = 0;
        }
        if (remaining == 0 || util_now_ms() >= deadline)
            return;
        util_sleep_ms(KILL_POLL_MS);
    }
}

/*
 * Function    : diag_kill
 * Description : kill: sends the signal (SIGTERM unless the client chose
 *               another one) to every matching process, waits up to
 *               KILL_WAIT_MS for them to exit and reports per process.
 *               The server process answering the request and PID 1 are
 *               never signalled.
 * Return      : STATUS_OK if every process terminated, STATUS_PARTIAL if
 *               some did, STATUS_FAILED if none did
 */
int diag_kill(const ProcList *list, int wire_signal, const char *name, StrBuf *out)
{
    int signo = proto_signal_native(wire_signal);
    const char *sig_name = proto_signal_name(wire_signal);
    pid_t self = getpid();
    unsigned char *state, *alive;
    int *errors;
    size_t i, terminated = 0;

    if (signo < 0 || sig_name == NULL)
    {
        sb_printf(out, "Signal not allowed\n");
        return STATUS_FAILED;
    }
    state = calloc(list->count, sizeof *state);
    alive = calloc(list->count, sizeof *alive);
    errors = calloc(list->count, sizeof *errors);
    if (state == NULL || alive == NULL || errors == NULL)
    {
        free(state);
        free(alive);
        free(errors);
        sb_printf(out, "The server ran out of memory\n");
        return STATUS_FAILED;
    }

    for (i = 0; i < list->count; i++)
    {
        const ProcEntry *e = &list->items[i];

        if (e->pid == self)
            state[i] = KILL_SKIP_SELF;
        else if (e->pid == INIT_PID)
            state[i] = KILL_SKIP_INIT;
        else if (!proc_is_alive(e))
            state[i] = KILL_ALREADY_GONE;   /* also protects against PID reuse */
        else if (kill(e->pid, signo) == 0)
        {
            state[i] = KILL_SENT;
            alive[i] = 1;
        }
        else if (errno == ESRCH)
            state[i] = KILL_ALREADY_GONE;
        else
        {
            state[i] = KILL_FAILED;
            errors[i] = errno;
        }
    }
    wait_for_exit(list, state, alive);

    for (i = 0; i < list->count; i++)
    {
        const ProcEntry *e = &list->items[i];
        char pname[NAME_TEXT_MAX];
        long pid = (long)e->pid;

        diag_name(e, pname, sizeof pname);
        switch (state[i])
        {
        case KILL_SENT:
            if (!alive[i])
            {
                sb_printf(out, "PID %ld (%s): SIG%s sent - process terminated\n", pid, pname,
                          sig_name);
                terminated++;
            }
            else
            {
                sb_printf(out, "PID %ld (%s): SIG%s sent - still running after %d ms "
                          "(it may catch or ignore SIG%s; try: kill %s KILL)\n",
                          pid, pname, sig_name, KILL_WAIT_MS, sig_name, name);
            }
            break;
        case KILL_ALREADY_GONE:
            sb_printf(out, "PID %ld (%s): had already exited\n", pid, pname);
            terminated++;
            break;
        case KILL_FAILED:
            sb_printf(out, "PID %ld (%s): could not send SIG%s: %s\n", pid, pname, sig_name,
                      strerror(errors[i]));
            break;
        case KILL_SKIP_SELF:
            sb_printf(out, "PID %ld (%s): skipped - this is the server process answering "
                      "this request\n", pid, pname);
            break;
        default:
            sb_printf(out, "PID %ld (%s): skipped - PID 1 (init) is never signalled\n", pid, pname);
            break;
        }
    }
    sb_printf(out, "%zu of %zu matching process%s terminated\n", terminated, list->count,
              list->count == 1 ? "" : "es");
    free(state);
    free(alive);
    free(errors);
    if (terminated == list->count)
        return STATUS_OK;
    return terminated > 0 ? STATUS_PARTIAL : STATUS_FAILED;
}
