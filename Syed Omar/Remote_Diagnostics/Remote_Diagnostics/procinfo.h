/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : procinfo.h
 *           Descriptions   : Readers for the /proc file system: process lookup by name,
 *                            /proc/<pid>/stat and status fields, open file descriptors
 *           Objective      : Collect process information without running any shell command
 *           Usage          : #include "procinfo.h"
 *           Output         : None (library module)
 *
###############################################################################################*/

#ifndef PROCINFO_H
#define PROCINFO_H

#include <stddef.h>
#include <sys/types.h>

#define PROC_ROOT          "/proc"
#define PROC_COMM_MAX      64       /* Room for the name in /proc/<pid>/stat          */
#define PROC_TASK_COMM_LEN 16       /* Kernel's TASK_COMM_LEN: names keep 15 chars    */
#define PROC_PATH_MAX      64       /* "/proc/<pid>/fdinfo/<fd>" and similar          */
#define PROC_LINK_MAX      4096     /* Longest readlink() target we keep              */
#define PROC_STAT_BUF      2048     /* Buffer for /proc/<pid>/stat                    */
#define PROC_STATUS_BUF    8192     /* Buffer for /proc/<pid>/status                  */
#define PROC_CMDLINE_BUF   4096     /* Bytes of /proc/<pid>/cmdline we look at        */
#define PROC_SMALL_BUF     512      /* Buffer for small files (uptime, fdinfo)        */
#define PROC_MAX_MATCHES   512      /* Most processes reported for one name           */
#define PROC_LIST_INITIAL  16       /* First allocation of a process / fd list        */

/* One matching process */
typedef struct
{
    pid_t pid;
    char  comm[PROC_COMM_MAX];          /* Kernel name (may hold any byte)       */
    unsigned long long start_ticks;     /* Start time; detects PID reuse         */
} ProcEntry;

/* Processes found for one name, sorted by PID */
typedef struct
{
    ProcEntry *items;
    size_t count;
    size_t capacity;
    int truncated;                      /* More than PROC_MAX_MATCHES matched    */
} ProcList;

/* Fields of /proc/<pid>/stat */
typedef struct
{
    char comm[PROC_COMM_MAX];
    char state;                         /* R, S, D, Z, T, ...                    */
    long ppid;
    unsigned long long utime;           /* User CPU time, clock ticks            */
    unsigned long long stime;           /* System CPU time, clock ticks          */
    long num_threads;
    unsigned long long start_ticks;     /* Start time after boot, clock ticks    */
} ProcStat;

int  proc_read_text(const char *path, char *buf, size_t size, size_t *length);
int  proc_read_stat(pid_t pid, ProcStat *st);
int  proc_find_by_name(const char *name, ProcList *list);
void proc_list_free(ProcList *list);
int  proc_is_alive(const ProcEntry *entry);

int  proc_field_kb(const char *text, const char *key, unsigned long long *value);
int  proc_meminfo_kb(const char *key, unsigned long long *value);
int  proc_uptime_seconds(double *seconds);

int  proc_list_fds(pid_t pid, int **fds, size_t *count);
int  proc_fd_target(pid_t pid, int fd, char *buf, size_t size);
int  proc_fd_flags(pid_t pid, int fd, unsigned long *flags);

#endif
