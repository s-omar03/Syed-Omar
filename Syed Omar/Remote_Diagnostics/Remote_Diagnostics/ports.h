/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : ports.h
 *           Descriptions   : Server-side command get-ports-used
 *           Objective      : List the TCP and UDP sockets (IPv4 and IPv6) of the matching
 *                            processes in a netstat-like table
 *           Usage          : #include "ports.h"
 *           Output         : None (library module)
 *
###############################################################################################*/

#ifndef PORTS_H
#define PORTS_H

#include "procinfo.h"
#include "strbuf.h"

#define PORTS_NOTES_MAX   (64 * 1024)   /* Size limit of the per-process notes */

int ports_report(const ProcList *list, StrBuf *out);

#endif
