/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : signal_pipe.h
 *           Descriptions   : "Self-pipe" signal handling shared by the server and the client
 *           Objective      : Turn SIGINT / SIGTERM / SIGHUP into a readable descriptor that
 *                            the main poll() loop can watch, without any race between
 *                            checking a flag and blocking in poll()
 *           Usage          : #include "signal_pipe.h"
 *           Output         : None (library module)
 *
###############################################################################################*/

#ifndef SIGNAL_PIPE_H
#define SIGNAL_PIPE_H

#include <stddef.h>

int signal_pipe_init(const int *signals, size_t count);
int signal_pipe_drain(int fd);

#endif
