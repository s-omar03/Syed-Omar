/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : lineedit.h
 *           Descriptions   : Minimal line editor for the client prompt (termios raw mode):
 *                            editing keys and Up/Down arrow history navigation
 *           Objective      : Read one command line; fall back to plain line reading when the
 *                            input is not a terminal (pipes, files, tests)
 *           Usage          : #include "lineedit.h"
 *           Output         : The prompt and the line being edited
 *
###############################################################################################*/

#ifndef LINEEDIT_H
#define LINEEDIT_H

#include <signal.h>
#include <stddef.h>
#include "history.h"

#define LE_LINE_MAX     HISTORY_LINE_MAX   /* Longest line incl. NUL           */
#define LE_PROMPT_MAX   32                 /* Longest prompt                   */

/* Results of le_read_line() */
typedef enum
{
    LE_OK = 0,          /* a line was read (may be empty)              */
    LE_EOF,             /* end of input (Ctrl+D on an empty line)      */
    LE_INTERRUPTED,     /* the interrupt flag was set by a signal      */
    LE_TOO_LONG,        /* plain mode: line longer than the buffer     */
    LE_ERROR            /* read error                                  */
} LeResult;

void le_init(volatile sig_atomic_t *interrupt_flag);
int  le_is_interactive(void);
int  le_read_line(const char *prompt, char *buf, size_t size, const History *hist);
void le_restore(void);

#endif
