/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : diag.h
 *           Descriptions   : Server-side commands get-mem, get-cpu-usage, get-open-fd and kill
 *           Objective      : Turn /proc data for the matching processes into reply text
 *           Usage          : #include "diag.h"
 *           Output         : None (library module)
 *
###############################################################################################*/

#ifndef DIAG_H
#define DIAG_H

#include "procinfo.h"
#include "strbuf.h"

#define CPU_SAMPLE_MS     1000    /* Interval over which %CPU is measured            */
#define KILL_WAIT_MS      1000    /* How long kill waits for processes to exit       */
#define KILL_POLL_MS      20      /* Check interval while waiting                    */
#define FD_LIST_LIMIT     1024    /* Descriptors listed per process                  */
#define NAME_TEXT_MAX     (PROC_COMM_MAX * 4)  /* Escaped process name               */

int  diag_get_mem(const ProcList *list, StrBuf *out);
int  diag_get_cpu(const ProcList *list, StrBuf *out);
int  diag_get_open_fd(const ProcList *list, StrBuf *out);
int  diag_kill(const ProcList *list, int wire_signal, const char *name, StrBuf *out);
const char *diag_name(const ProcEntry *entry, char *buf, size_t size);
int  diag_status(size_t total, size_t failed);

#endif
