/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : session.c
 *           Descriptions   : Client session threads. A session sends the welcome reply, then
 *                            loops: receive one packet, handle it (login phase until the user
 *                            is authenticated, chat phase afterwards), until the connection
 *                            ends. The manager counts sessions and stops them at shutdown.
 *           Objective      : Serve many clients at the same time, each with a small stack,
 *                            with a fixed upper limit on the number of connections
 *           Usage          : Used by server.c through session.h
 *           Output         : Replies to the client, activity log lines
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* clock_gettime() */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "logger.h"
#include "protocol.h"
#include "session.h"
#include "tcp.h"

#if SERVER_MAX_SESSIONS > PROTO_MAX_LIST_USERS
#error "A USER_LIST packet must be able to hold every online user"
#endif

/* State of one client connection, owned by its thread */
typedef struct
{
    SessionManager *mgr;
    int fd;                             /* client socket                          */
    int slot;                           /* index in mgr->fds                      */
    int failures;                       /* rejected requests since the last login */
    char peer[TCP_PEER_NAME_SIZE];      /* "address:port" for the log             */
    char user[PROTO_NAME_SIZE];         /* logged-in user, "" in the login phase  */
} Session;

/*
 * Function    : claim_slot
 * Description : Reserves a session slot for socket fd
 * Return      : Slot index, or -1 when the server is full or stopping
 */
static int claim_slot(SessionManager *mgr, int fd)
{
    int slot = -1, i;

    pthread_mutex_lock(&mgr->lock);
    if (!mgr->stopping && mgr->count < SERVER_MAX_SESSIONS)
    {
        for (i = 0; i < SERVER_MAX_SESSIONS; i++)
        {
            if (mgr->fds[i] < 0)
            {
                mgr->fds[i] = fd;
                mgr->count++;
                slot = i;
                break;
            }
        }
    }
    pthread_mutex_unlock(&mgr->lock);
    return slot;
}

/*
 * Function    : release_slot
 * Description : Frees a session slot; wakes session_shutdown_all() when the
 *               last session has ended. Must happen before the socket is
 *               closed, so shutdown_all never touches a closed descriptor.
 */
static void release_slot(SessionManager *mgr, int slot)
{
    pthread_mutex_lock(&mgr->lock);
    mgr->fds[slot] = -1;
    mgr->count--;
    if (mgr->count == 0)
        pthread_cond_broadcast(&mgr->idle);
    pthread_mutex_unlock(&mgr->lock);
}

/*
 * Function    : reject_request
 * Description : Answers a refused login-phase request. After
 *               SESSION_MAX_AUTH_FAILURES refusals the client gets a fatal
 *               reply and the connection is closed.
 * Return      : 1 to keep the session running, 0 to end it
 */
static int reject_request(Session *s, const char *reason)
{
    s->failures++;
    if (s->failures >= SESSION_MAX_AUTH_FAILURES)
    {
        log_info("%s: too many failed attempts, closing connection", s->peer);
        room_reply(s->mgr->room, s->fd, REPLY_FATAL, REPLY_TOO_MANY_FAILURES);
        return 0;
    }
    room_reply(s->mgr->room, s->fd, REPLY_ERROR, reason);
    return 1;
}

/*
 * Function    : handle_auth_request
 * Description : Register or login request (spec 3.1). On success the room
 *               has already sent the reply, the online list and the
 *               ONLINE notices.
 * Return      : 1 to keep the session running, 0 to end it
 */
static int handle_auth_request(Session *s, AuthRequest *req)
{
    const char *action = (req->option == AUTH_REGISTER) ? "registration" : "login";
    RoomStatus status;

    if (req->option == AUTH_REGISTER)
        status = room_register(s->mgr->room, req->username, req->password, s->fd);
    else if (req->option == AUTH_LOGIN)
        status = room_login(s->mgr->room, req->username, req->password, s->fd);
    else
        return reject_request(s, REPLY_INVALID_OPTION);
    memset(req->password, 0, sizeof req->password);

    if (status == ROOM_OK)
    {
        proto_copy_text(s->user, sizeof s->user, req->username);
        s->failures = 0;
        log_info("%s %s as '%s'", s->peer,
                 req->option == AUTH_REGISTER ? "registered" : "logged in", s->user);
        return 1;
    }

    /* Only names that passed validation are logged, so the log stays clean */
    if (proto_valid_username(req->username))
        log_info("%s: %s of '%s' refused: %s", s->peer, action, req->username,
                 room_status_text(status));
    else
        log_info("%s: %s refused: invalid user-name", s->peer, action);

    if (status == ROOM_DB_FULL || status == ROOM_DB_ERROR)
    {
        room_reply(s->mgr->room, s->fd, REPLY_ERROR, room_status_text(status));
        return 1;               /* A server problem, not the client's fault */
    }
    return reject_request(s, room_status_text(status));
}

/*
 * Function    : handle_login_phase
 * Description : Packets from a client that is not logged in. Only register
 *               and login are accepted.
 * Return      : 1 to keep the session running, 0 to end it
 */
static int handle_login_phase(Session *s, Packet *pkt)
{
    switch (pkt->type)
    {
    case PKT_AUTH_REQUEST:
        return handle_auth_request(s, &pkt->body.auth);
    case PKT_CHAT:
        if (pkt->body.chat.chat_option == CHAT_LOGOUT)
            return reject_request(s, REPLY_LOGOUT_FAILED);
        return reject_request(s, REPLY_LOGIN_FIRST);
    case PKT_LIST_REQUEST:
        return reject_request(s, REPLY_LOGIN_FIRST);
    default:
        log_info("%s sent unexpected packet type %u, closing connection", s->peer,
                 (unsigned)pkt->type);
        return 0;
    }
}

/*
 * Function    : handle_chat_message
 * Description : Single chat, group chat or logout (spec 3.3 - 3.5). The
 *               sender field of the packet is ignored: the room always uses
 *               the logged-in name of this session.
 */
static void handle_chat_message(Session *s, const ChatMessage *msg)
{
    ChatRoom *room = s->mgr->room;
    RoomStatus status;
    size_t delivered;

    switch (msg->chat_option)
    {
    case CHAT_SINGLE:
        status = room_send_private(room, s->user, s->fd, msg->receiver, msg->message);
        if (status == ROOM_OK)
            log_info("'%s' sent a private message to '%s'", s->user, msg->receiver);
        else
            log_info("'%s': private message refused: %s", s->user, room_status_text(status));
        break;

    case CHAT_GROUP:
        status = room_send_group(room, s->user, s->fd, msg->message, &delivered);
        if (status == ROOM_OK)
            log_info("'%s' sent a group message to %zu user(s)", s->user, delivered);
        else
            log_info("'%s': group message refused: %s", s->user, room_status_text(status));
        break;

    case CHAT_LOGOUT:
        if (room_logout(room, s->user, s->fd) == ROOM_OK)
        {
            log_info("'%s' logged out", s->user);
            s->user[0] = '\0';
        }
        break;

    default:
        room_reply(room, s->fd, REPLY_ERROR, REPLY_INVALID_CHAT_OPT);
        break;
    }
}

/*
 * Function    : handle_chat_phase
 * Description : Packets from a logged-in client
 * Return      : 1 to keep the session running, 0 to end it
 */
static int handle_chat_phase(Session *s, Packet *pkt)
{
    switch (pkt->type)
    {
    case PKT_CHAT:
        handle_chat_message(s, &pkt->body.chat);
        return 1;
    case PKT_LIST_REQUEST:
        room_send_list(s->mgr->room, s->user, s->fd);
        return 1;
    case PKT_AUTH_REQUEST:
        memset(pkt->body.auth.password, 0, sizeof pkt->body.auth.password);
        room_reply(s->mgr->room, s->fd, REPLY_ERROR, REPLY_ALREADY_LOGGED_IN);
        return 1;
    default:
        log_info("%s sent unexpected packet type %u, closing connection", s->peer,
                 (unsigned)pkt->type);
        return 0;
    }
}

/*
 * Function    : session_thread
 * Description : Body of a session thread. When the connection ends for any
 *               reason, a logged-in user is marked offline (and the others
 *               are told) before the socket is closed.
 */
static void *session_thread(void *arg)
{
    Session *s = arg;
    SessionManager *mgr = s->mgr;
    Packet pkt;
    int running = 1, fd, slot, rc;

    tcp_peer_name(s->fd, s->peer, sizeof s->peer);
    log_info("%s connected", s->peer);
    room_reply(mgr->room, s->fd, REPLY_OK, REPLY_WELCOME);

    while (running)
    {
        rc = proto_recv_packet(s->fd, &pkt);
        if (rc != PROTO_OK)
        {
            if (rc == PROTO_ERR_BAD)
                log_info("%s sent a malformed packet (protocol error), closing connection",
                         s->peer);
            break;
        }
        running = (s->user[0] == '\0') ? handle_login_phase(s, &pkt) : handle_chat_phase(s, &pkt);
    }
    memset(&pkt, 0, sizeof pkt);

    if (s->user[0] != '\0' && room_disconnect(mgr->room, s->user, s->fd))
        log_info("'%s' went offline without logging out", s->user);
    log_info("%s disconnected", s->peer);

    fd = s->fd;
    slot = s->slot;
    free(s);
    release_slot(mgr, slot);
    close(fd);
    return NULL;
}

/*
 * Function    : session_manager_init
 * Description : Prepares an empty session manager for 'room'
 * Return      : 0 on success, -1 with errno set on error
 */
int session_manager_init(SessionManager *mgr, ChatRoom *room)
{
    int i, rc;

    memset(mgr, 0, sizeof *mgr);
    mgr->room = room;
    for (i = 0; i < SERVER_MAX_SESSIONS; i++)
        mgr->fds[i] = -1;
    rc = pthread_mutex_init(&mgr->lock, NULL);
    if (rc == 0)
    {
        rc = pthread_cond_init(&mgr->idle, NULL);
        if (rc != 0)
            pthread_mutex_destroy(&mgr->lock);
    }
    if (rc != 0)
    {
        errno = rc;
        return -1;
    }
    return 0;
}

/*
 * Function    : session_manager_destroy
 * Description : Frees the manager. No session may be running.
 */
void session_manager_destroy(SessionManager *mgr)
{
    pthread_cond_destroy(&mgr->idle);
    pthread_mutex_destroy(&mgr->lock);
}

/*
 * Function    : session_start
 * Description : Starts a detached session thread for an accepted socket.
 *               On SESSION_FULL or SESSION_ERROR the caller still owns fd.
 * Return      : SESSION_STARTED, SESSION_FULL, or SESSION_ERROR (errno set)
 */
int session_start(SessionManager *mgr, int fd)
{
    pthread_attr_t attr;
    pthread_t thread;
    Session *s;
    int slot, rc;

    slot = claim_slot(mgr, fd);
    if (slot < 0)
        return SESSION_FULL;
    s = calloc(1, sizeof *s);
    if (s == NULL)
    {
        release_slot(mgr, slot);
        errno = ENOMEM;
        return SESSION_ERROR;
    }
    s->mgr = mgr;
    s->fd = fd;
    s->slot = slot;

    rc = pthread_attr_init(&attr);
    if (rc == 0)
    {
        rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (rc == 0)
            rc = pthread_attr_setstacksize(&attr, SESSION_STACK_SIZE);
        if (rc == 0)
            rc = pthread_create(&thread, &attr, session_thread, s);
        pthread_attr_destroy(&attr);
    }
    if (rc != 0)
    {
        free(s);
        release_slot(mgr, slot);
        errno = rc;
        return SESSION_ERROR;
    }
    return SESSION_STARTED;
}

/*
 * Function    : session_count
 * Description : Number of running sessions
 */
int session_count(SessionManager *mgr)
{
    int count;

    pthread_mutex_lock(&mgr->lock);
    count = mgr->count;
    pthread_mutex_unlock(&mgr->lock);
    return count;
}

/*
 * Function    : session_shutdown_all
 * Description : Refuses new sessions, shuts every session socket down (the
 *               threads see EOF and finish normally) and waits up to
 *               'wait_seconds' for all of them to end
 * Return      : 1 if every session ended, 0 on timeout
 */
int session_shutdown_all(SessionManager *mgr, int wait_seconds)
{
    struct timespec deadline;
    int i, remaining;

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += wait_seconds;

    pthread_mutex_lock(&mgr->lock);
    mgr->stopping = 1;
    for (i = 0; i < SERVER_MAX_SESSIONS; i++)
    {
        if (mgr->fds[i] >= 0)
            shutdown(mgr->fds[i], SHUT_RDWR);
    }
    while (mgr->count > 0)
    {
        if (pthread_cond_timedwait(&mgr->idle, &mgr->lock, &deadline) == ETIMEDOUT)
            break;
    }
    remaining = mgr->count;
    pthread_mutex_unlock(&mgr->lock);
    return remaining == 0;
}
