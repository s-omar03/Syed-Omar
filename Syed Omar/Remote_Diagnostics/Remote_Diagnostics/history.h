/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : history.h
 *           Descriptions   : Command history of the client: array of at most 50 commands,
 *                            saved to .remote_history on exit and loaded on start
 *           Objective      : Support the "history" command and Up/Down arrow navigation
 *           Usage          : #include "history.h"
 *           Output         : None (library module)
 *
###############################################################################################*/

#ifndef HISTORY_H
#define HISTORY_H

#include <stddef.h>
#include <stdio.h>

#define HISTORY_MAX        50                  /* Commands kept (spec: maximum 50)   */
#define HISTORY_LINE_MAX   256                 /* Longest command line incl. NUL     */
#define HISTORY_FILE_NAME  ".remote_history"   /* File name in $HOME                 */
#define HISTORY_PATH_MAX   4096                /* Longest history file path          */

/* Oldest command first */
typedef struct
{
    char entries[HISTORY_MAX][HISTORY_LINE_MAX];
    size_t count;
} History;

void   history_init(History *h);
void   history_add(History *h, const char *line);
size_t history_count(const History *h);
const char *history_get(const History *h, size_t index);
void   history_print(const History *h, FILE *out);
int    history_default_path(char *buf, size_t size);
int    history_load(History *h, const char *path);
int    history_save(const History *h, const char *path);

#endif
