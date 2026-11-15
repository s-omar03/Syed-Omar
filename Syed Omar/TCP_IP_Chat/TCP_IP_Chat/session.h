/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : session.h
 *           Descriptions   : One detached thread per connected client ("session"), and the
 *                            manager that limits and tracks the running sessions
 *           Objective      : Make the server concurrent (spec 2.1.2): every client is served
 *                            by its own thread, first in the login phase, then in the chat
 *                            phase
 *           Usage          : #include "session.h"
 *           Output         : None (library module)
 *
###############################################################################################*/

#ifndef SESSION_H
#define SESSION_H

#include <pthread.h>
#include "chat_room.h"

#define SERVER_MAX_SESSIONS         64              /* connections served at once      */
#define SESSION_STACK_SIZE          (256 * 1024)    /* bytes of stack per thread       */
#define SESSION_MAX_AUTH_FAILURES   5               /* then the connection is closed   */
#define SESSION_SHUTDOWN_WAIT_SEC   5               /* wait for threads at server exit */

/* session_start() results */
#define SESSION_STARTED             0
#define SESSION_FULL                1
#define SESSION_ERROR               -1

typedef struct
{
    ChatRoom *room;
    pthread_mutex_t lock;               /* protects every field below             */
    pthread_cond_t idle;                /* signalled when count drops to zero     */
    int fds[SERVER_MAX_SESSIONS];       /* socket of each running session, or -1  */
    int count;                          /* running sessions                       */
    int stopping;                       /* set by session_shutdown_all()          */
} SessionManager;

int session_manager_init(SessionManager *mgr, ChatRoom *room);
void session_manager_destroy(SessionManager *mgr);
int session_start(SessionManager *mgr, int fd);
int session_count(SessionManager *mgr);
int session_shutdown_all(SessionManager *mgr, int wait_seconds);

#endif
