/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : ports.c
 *           Descriptions   : Server-side command get-ports-used
 *           Objective      : List the TCP and UDP sockets of the matching processes:
 *                            1. every descriptor in /proc/<pid>/fd that links to
 *                               "socket:[inode]" gives one socket inode;
 *                            2. /proc/<pid>/net/{tcp,tcp6,udp,udp6} list every socket of
 *                               the process's network namespace with its inode;
 *                            3. the lines whose inode belongs to the process are printed.
 *           Usage          : Linked into ./server
 *           Output         : netstat-like table in a StrBuf
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* inet_ntop() */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "diag.h"
#include "ports.h"
#include "protocol.h"

#define NET_LINE_MAX          512       /* Longest line of /proc/net/tcp kept       */
#define NET_HEX_V4            8         /* Hex digits of an IPv4 address            */
#define NET_HEX_V6            32        /* Hex digits of an IPv6 address            */
#define NET_HEX_WORD          8         /* The kernel prints 32-bit words           */
#define NET_ENDPOINT_MAX      64        /* "[IPv6]:port" text                       */
#define NET_FIELD_COUNT       8         /* Fields parsed from a socket line         */
#define NET_PORT_MAX          65535u
#define NET_ADDR_BYTES        16        /* Room for an IPv6 address                 */
#define SOCKET_LINK_PREFIX    "socket:["
#define TCP_STATE_ESTABLISHED 0x01
#define TCP_STATE_CLOSE       0x07
#define DECIMAL_BASE          10
#define INODE_LIST_INITIAL    16

/* Column layout, like "netstat -anp" */
#define PORTS_HEADER_FMT "%-5s %6s %6s %-23s %-23s %-11s %s\n"
#define PORTS_ROW_FMT    "%-5s %6lu %6lu %-23s %-23s %-11s %ld/%s\n"

/* One socket table of the kernel */
typedef struct
{
    const char *file;       /* File name under /proc/<pid>/net */
    int family;             /* AF_INET or AF_INET6             */
    int is_tcp;             /* 1 = TCP, 0 = UDP                */
} NetTable;

static const NetTable NET_TABLES[] =
{
    { "tcp",  AF_INET,  1 },
    { "tcp6", AF_INET6, 1 },
    { "udp",  AF_INET,  0 },
    { "udp6", AF_INET6, 0 }
};

#define NET_TABLE_COUNT (sizeof NET_TABLES / sizeof NET_TABLES[0])

/* TCP states as numbered by the kernel (include/net/tcp_states.h) */
static const char *const TCP_STATES[] =
{
    "UNKNOWN", "ESTABLISHED", "SYN_SENT", "SYN_RECV", "FIN_WAIT1", "FIN_WAIT2",
    "TIME_WAIT", "CLOSE", "CLOSE_WAIT", "LAST_ACK", "LISTEN", "CLOSING", "NEW_SYN_RECV"
};

#define TCP_STATE_COUNT (sizeof TCP_STATES / sizeof TCP_STATES[0])

/* Socket inodes of one process */
typedef struct
{
    unsigned long *inodes;      /* sorted, without duplicates          */
    unsigned char *seen;        /* 1 = found in a TCP/UDP table        */
    size_t count;
} InodeSet;

/* One parsed line of a socket table */
typedef struct
{
    unsigned char local[NET_ADDR_BYTES];
    unsigned char remote[NET_ADDR_BYTES];
    unsigned int local_port;
    unsigned int remote_port;
    unsigned int state;
    unsigned long tx_queue;
    unsigned long rx_queue;
    unsigned long inode;
} NetEntry;

/*
 * Function    : compare_inodes
 * Description : qsort()/bsearch() callback for inode numbers
 */
static int compare_inodes(const void *a, const void *b)
{
    unsigned long x = *(const unsigned long *)a, y = *(const unsigned long *)b;

    return (x > y) - (x < y);
}

/*
 * Function    : parse_socket_link
 * Description : Extracts the inode from a "socket:[12345]" link target
 * Return      : 0 and *inode on success, -1 if the link is not a socket
 */
static int parse_socket_link(const char *link, unsigned long *inode)
{
    size_t prefix_len = strlen(SOCKET_LINK_PREFIX);
    char *end;

    if (strncmp(link, SOCKET_LINK_PREFIX, prefix_len) != 0)
        return -1;
    if (link[prefix_len] < '0' || link[prefix_len] > '9')
        return -1;
    errno = 0;
    *inode = strtoul(link + prefix_len, &end, DECIMAL_BASE);
    if (errno != 0 || *end != ']' || end[1] != '\0')
        return -1;
    return 0;
}

/*
 * Function    : collect_socket_inodes
 * Description : Collects the inodes of all sockets the process has open
 * Return      : 0 on success, -1 on error (errno set)
 */
static int collect_socket_inodes(pid_t pid, InodeSet *set)
{
    char link[PROC_LINK_MAX];
    int *fds;
    size_t nfds, i, used = 0, capacity = 0, unique = 0;
    unsigned long *list = NULL;

    set->inodes = NULL;
    set->seen = NULL;
    set->count = 0;
    if (proc_list_fds(pid, &fds, &nfds) != 0)
        return -1;
    for (i = 0; i < nfds; i++)
    {
        unsigned long inode;

        if (proc_fd_target(pid, fds[i], link, sizeof link) != 0
            || parse_socket_link(link, &inode) != 0)
            continue;
        if (used == capacity)
        {
            size_t new_capacity = capacity == 0 ? INODE_LIST_INITIAL : capacity * 2;
            unsigned long *p = realloc(list, new_capacity * sizeof *p);

            if (p == NULL)
            {
                free(list);
                free(fds);
                errno = ENOMEM;
                return -1;
            }
            list = p;
            capacity = new_capacity;
        }
        list[used++] = inode;
    }
    free(fds);
    if (used == 0)
        return 0;

    /* Several descriptors can share one socket (dup(), fork()): keep each inode once */
    qsort(list, used, sizeof list[0], compare_inodes);
    for (i = 0; i < used; i++)
    {
        if (unique == 0 || list[unique - 1] != list[i])
            list[unique++] = list[i];
    }
    set->seen = calloc(unique, sizeof *set->seen);
    if (set->seen == NULL)
    {
        free(list);
        errno = ENOMEM;
        return -1;
    }
    set->inodes = list;
    set->count = unique;
    return 0;
}

/*
 * Function    : free_inode_set
 * Description : Releases an InodeSet
 */
static void free_inode_set(InodeSet *set)
{
    free(set->inodes);
    free(set->seen);
    set->inodes = NULL;
    set->seen = NULL;
    set->count = 0;
}

/*
 * Function    : hex_value
 * Description : Value of one hexadecimal digit
 * Return      : 0-15, or -1 if the character is not a hex digit
 */
static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/*
 * Function    : hex_to_address
 * Description : Converts the kernel's address text to address bytes. The
 *               kernel prints the address as 32-bit words in the machine's
 *               own byte order, so each word is parsed as a number and its
 *               bytes copied unchanged; this works on any CPU.
 * Return      : 0 on success, -1 on malformed text
 */
static int hex_to_address(const char *hex, int family, unsigned char *addr)
{
    size_t digits = family == AF_INET ? NET_HEX_V4 : NET_HEX_V6;
    size_t w, j;

    if (strlen(hex) != digits)
        return -1;
    for (w = 0; w < digits / NET_HEX_WORD; w++)
    {
        uint32_t word = 0;

        for (j = 0; j < NET_HEX_WORD; j++)
        {
            int v = hex_value(hex[w * NET_HEX_WORD + j]);

            if (v < 0)
                return -1;
            word = (word << 4) | (uint32_t)v;
        }
        memcpy(addr + w * sizeof word, &word, sizeof word);
    }
    return 0;
}

/*
 * Function    : parse_net_line
 * Description : Parses one line of /proc/net/{tcp,udp}[6]:
 *                 sl local_address rem_address st tx_queue:rx_queue
 *                 tr:tm->when retrnsmt uid timeout inode ...
 * Return      : 0 on success, -1 for the header line or malformed lines
 */
static int parse_net_line(const char *line, int family, NetEntry *e)
{
    char local_hex[NET_HEX_V6 + 1], remote_hex[NET_HEX_V6 + 1];

    if (sscanf(line, " %*u: %32[0-9A-Fa-f]:%x %32[0-9A-Fa-f]:%x %x %lx:%lx %*s %*s %*s %*s %lu",
               local_hex, &e->local_port, remote_hex, &e->remote_port, &e->state,
               &e->tx_queue, &e->rx_queue, &e->inode) != NET_FIELD_COUNT)
        return -1;
    if (e->local_port > NET_PORT_MAX || e->remote_port > NET_PORT_MAX)
        return -1;
    if (hex_to_address(local_hex, family, e->local) != 0
        || hex_to_address(remote_hex, family, e->remote) != 0)
        return -1;
    return 0;
}

/*
 * Function    : format_endpoint
 * Description : "a.b.c.d:port" or "[v6]:port"; port 0 is shown as '*'
 */
static void format_endpoint(int family, const unsigned char *addr, unsigned int port,
                            char *buf, size_t size)
{
    char ip[INET6_ADDRSTRLEN];

    if (inet_ntop(family, addr, ip, sizeof ip) == NULL)
        snprintf(ip, sizeof ip, "?");
    if (family == AF_INET6)
    {
        if (port == 0)
            snprintf(buf, size, "[%s]:*", ip);
        else
            snprintf(buf, size, "[%s]:%u", ip, port);
    }
    else
    {
        if (port == 0)
            snprintf(buf, size, "%s:*", ip);
        else
            snprintf(buf, size, "%s:%u", ip, port);
    }
}

/*
 * Function    : state_text
 * Description : State column. UDP has no connection state: like netstat,
 *               a connected UDP socket shows ESTABLISHED, others nothing.
 */
static const char *state_text(const NetTable *table, unsigned int state)
{
    if (!table->is_tcp)
        return state == TCP_STATE_ESTABLISHED ? "ESTABLISHED" : "";
    if (state < TCP_STATE_COUNT)
        return TCP_STATES[state];
    return "UNKNOWN";
}

/*
 * Function    : open_net_table
 * Description : Opens /proc/<pid>/net/<file>, the table of the network
 *               namespace the process lives in. Falls back to /proc/net.
 * Return      : The stream, or NULL (for example IPv6 disabled)
 */
static FILE *open_net_table(pid_t pid, const char *file)
{
    char path[PROC_PATH_MAX];
    FILE *fp;

    snprintf(path, sizeof path, PROC_ROOT "/%ld/net/%s", (long)pid, file);
    fp = fopen(path, "r");
    if (fp == NULL)
    {
        snprintf(path, sizeof path, PROC_ROOT "/net/%s", file);
        fp = fopen(path, "r");
    }
    return fp;
}

/*
 * Function    : scan_table
 * Description : Prints every line of one socket table whose inode belongs
 *               to the process. The header is printed before the first row.
 * Return      : Number of rows printed
 */
static size_t scan_table(const ProcEntry *proc, const char *pname, const NetTable *table,
                         InodeSet *set, StrBuf *out, size_t *rows_total)
{
    char line[NET_LINE_MAX];
    size_t rows = 0;
    FILE *fp = open_net_table(proc->pid, table->file);

    if (fp == NULL)
        return 0;
    while (fgets(line, sizeof line, fp) != NULL)
    {
        char local[NET_ENDPOINT_MAX], remote[NET_ENDPOINT_MAX];
        unsigned long *hit;
        NetEntry e;

        if (parse_net_line(line, table->family, &e) != 0 || e.inode == 0)
            continue;
        hit = bsearch(&e.inode, set->inodes, set->count, sizeof set->inodes[0], compare_inodes);
        if (hit == NULL)
            continue;
        set->seen[hit - set->inodes] = 1;
        if (*rows_total == 0)
            sb_printf(out, PORTS_HEADER_FMT, "Proto", "Recv-Q", "Send-Q", "Local Address",
                      "Foreign Address", "State", "PID/Program name");
        format_endpoint(table->family, e.local, e.local_port, local, sizeof local);
        format_endpoint(table->family, e.remote, e.remote_port, remote, sizeof remote);
        sb_printf(out, PORTS_ROW_FMT, table->file, e.rx_queue, e.tx_queue, local, remote,
                  state_text(table, e.state), (long)proc->pid, pname);
        rows++;
        (*rows_total)++;
    }
    fclose(fp);
    return rows;
}

/*
 * Function    : ports_report
 * Description : get-ports-used: netstat-like rows for every TCP/UDP socket
 *               of every matching process, then a note for processes
 *               without such sockets or with other sockets (UNIX domain,
 *               netlink, ...) that have no port.
 * Return      : ReplyStatus
 */
int ports_report(const ProcList *list, StrBuf *out)
{
    StrBuf notes;
    size_t i, t, rows_total = 0, failed = 0;

    sb_init(&notes, PORTS_NOTES_MAX);
    for (i = 0; i < list->count; i++)
    {
        const ProcEntry *e = &list->items[i];
        char pname[NAME_TEXT_MAX];
        size_t rows = 0, other = 0, k;
        InodeSet set;

        diag_name(e, pname, sizeof pname);
        if (collect_socket_inodes(e->pid, &set) != 0)
        {
            int err = errno;

            sb_printf(&notes, "PID %ld (%s): cannot read the open files: %s\n", (long)e->pid,
                      pname, err == ENOENT ? "the process has exited" : strerror(err));
            failed++;
            continue;
        }
        if (set.count > 0)
        {
            for (t = 0; t < NET_TABLE_COUNT; t++)
                rows += scan_table(e, pname, &NET_TABLES[t], &set, out, &rows_total);
        }
        for (k = 0; k < set.count; k++)
            other += set.seen[k] ? 0 : 1;
        if (rows == 0 && other == 0)
            sb_printf(&notes, "PID %ld (%s): no sockets open\n", (long)e->pid, pname);
        else if (rows == 0)
            sb_printf(&notes, "PID %ld (%s): no TCP/UDP ports, %zu other socket%s "
                      "(UNIX domain, netlink, ...)\n", (long)e->pid, pname, other,
                      other == 1 ? "" : "s");
        else if (other > 0)
            sb_printf(&notes, "PID %ld (%s): also %zu other socket%s without a port "
                      "(UNIX domain, netlink, ...)\n", (long)e->pid, pname, other,
                      other == 1 ? "" : "s");
        free_inode_set(&set);
    }
    if (notes.length > 0)
        sb_printf(out, "%s", sb_text(&notes));
    sb_free(&notes);
    return diag_status(list->count, failed);
}
