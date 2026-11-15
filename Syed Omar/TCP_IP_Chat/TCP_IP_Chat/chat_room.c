/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : chat_room.c
 *           Descriptions   : Chat room operations. Each one takes the room mutex, works on the
 *                            user database and sends the resulting packets before unlocking.
 *           Objective      : Implement the server side of spec 2.1 (register, login, online
 *                            list, join / leave notices, logout, single and group chat)
 *           Usage          : Used by session.c and server.c through chat_room.h
 *           Output         : Packets to the connected clients, activity log lines
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include "chat_room.h"
#include "logger.h"

/*
 * Why every send happens under the lock
 * -------------------------------------
 * Several session threads may want to write to the same client socket at
 * the same time (a group message, a join notice and that client's own
 * reply). Holding the room mutex while sending keeps every packet whole.
 * It also makes the user's socket descriptor safe to use: a session marks
 * its user offline under the lock BEFORE it closes the socket, so a
 * descriptor found in the database is always still open.
 *
 * A send that fails (peer gone, or blocked longer than the send timeout)
 * shuts that socket down. Its own session thread then receives EOF, takes
 * the lock and cleans up, so no thread ever closes another thread's socket.
 */

/*
 * Function    : check_sent
 * Description : Shuts a socket down when sending to it failed
 * Return      : 0 if the packet was sent, -1 if it failed
 */
static int check_sent(int rc, int fd)
{
    if (rc == PROTO_OK)
        return 0;
    shutdown(fd, SHUT_RDWR);
    return -1;
}

/*
 * Function    : is_online
 * Description : True when the user is logged in on some session
 */
static int is_online(const UserRecord *user)
{
    return user->status == STATUS_ONLINE && user->sockfd >= 0;
}

/*
 * Function    : notify_others_locked
 * Description : Sends "user is ONLINE / OFFLINE" to every other online user
 *               (spec 2.1.8). Caller holds the lock.
 */
static void notify_others_locked(ChatRoom *room, const char *username, uint32_t status)
{
    size_t i;

    for (i = 0; i < room->db.count; i++)
    {
        UserRecord *other = &room->db.users[i];

        if (is_online(other) && strcmp(other->username, username) != 0)
            check_sent(proto_send_status(other->sockfd, username, status), other->sockfd);
    }
}

/*
 * Function    : send_list_locked
 * Description : Sends the names of all online users except 'username'
 *               (spec 2.1.7). Caller holds the lock.
 */
static void send_list_locked(ChatRoom *room, const char *username, int fd)
{
    char names[PROTO_MAX_LIST_USERS][PROTO_NAME_SIZE];
    uint32_t count = 0;
    size_t i;

    for (i = 0; i < room->db.count && count < PROTO_MAX_LIST_USERS; i++)
    {
        const UserRecord *user = &room->db.users[i];

        if (is_online(user) && strcmp(user->username, username) != 0)
            proto_copy_text(names[count++], PROTO_NAME_SIZE, user->username);
    }
    check_sent(proto_send_user_list(fd, names, count), fd);
}

/*
 * Function    : go_online_locked
 * Description : Marks a user online on socket fd, then sends the success
 *               reply and the online list to the user and the ONLINE notice
 *               to everyone else, in that order. Caller holds the lock.
 */
static void go_online_locked(ChatRoom *room, UserRecord *user, int fd, const char *reply)
{
    user->status = STATUS_ONLINE;
    user->sockfd = fd;
    if (check_sent(proto_send_reply(fd, REPLY_OK, reply), fd) == 0)
        send_list_locked(room, user->username, fd);
    notify_others_locked(room, user->username, STATUS_ONLINE);
}

/*
 * Function    : go_offline_locked
 * Description : Clears the user's online entry and tells the others.
 *               Caller holds the lock.
 */
static void go_offline_locked(ChatRoom *room, UserRecord *user)
{
    user->status = STATUS_OFFLINE;
    user->sockfd = -1;
    notify_others_locked(room, user->username, STATUS_OFFLINE);
}

/*
 * Function    : room_init
 * Description : Loads the user database and creates the room mutex
 * Return      : 0 on success, -1 with errno set on error. *skipped is the
 *               number of invalid lines ignored in the database file.
 */
int room_init(ChatRoom *room, const char *db_path, size_t *skipped)
{
    int rc;

    memset(room, 0, sizeof *room);
    rc = user_db_load(&room->db, db_path, skipped);
    if (rc != USER_DB_OK)
    {
        if (rc == USER_DB_ERR_NOMEM)
            errno = ENOMEM;
        return -1;
    }
    rc = pthread_mutex_init(&room->lock, NULL);
    if (rc != 0)
    {
        user_db_free(&room->db);
        errno = rc;
        return -1;
    }
    return 0;
}

/*
 * Function    : room_destroy
 * Description : Frees the room. No session thread may be running.
 */
void room_destroy(ChatRoom *room)
{
    pthread_mutex_destroy(&room->lock);
    user_db_free(&room->db);
}

/*
 * Function    : room_user_count
 * Description : Number of registered users
 */
size_t room_user_count(ChatRoom *room)
{
    size_t count;

    pthread_mutex_lock(&room->lock);
    count = room->db.count;
    pthread_mutex_unlock(&room->lock);
    return count;
}

/*
 * Function    : room_register
 * Description : Registers a new user and logs it in on socket fd. On
 *               success the replies are sent here; on failure nothing is
 *               sent and the caller replies with room_status_text().
 * Return      : ROOM_OK, ROOM_INVALID_USERNAME, ROOM_INVALID_PASSWORD,
 *               ROOM_DUPLICATE, ROOM_DB_FULL or ROOM_DB_ERROR
 */
RoomStatus room_register(ChatRoom *room, const char *username, const char *password, int fd)
{
    UserRecord *user;
    RoomStatus status = ROOM_OK;
    char err[LOG_ERROR_TEXT_SIZE];
    int rc;

    if (!proto_valid_username(username))
        return ROOM_INVALID_USERNAME;
    if (!proto_valid_password(password))
        return ROOM_INVALID_PASSWORD;

    pthread_mutex_lock(&room->lock);
    if (user_db_find(&room->db, username) != NULL)
    {
        status = ROOM_DUPLICATE;
    }
    else
    {
        rc = user_db_add(&room->db, username, password, &user);
        if (rc == USER_DB_OK)
            go_online_locked(room, user, fd, REPLY_REGISTER_OK);
        else if (rc == USER_DB_ERR_FULL)
            status = ROOM_DB_FULL;
        else
        {
            log_error("Could not add user to %s: %s", room->db.path,
                      rc == USER_DB_ERR_NOMEM ? "out of memory"
                                              : log_errno_text(errno, err, sizeof err));
            status = ROOM_DB_ERROR;
        }
    }
    pthread_mutex_unlock(&room->lock);
    return status;
}

/*
 * Function    : room_login
 * Description : Logs a registered user in on socket fd. The password is
 *               checked before the online state, so "already logged in"
 *               is never revealed to someone without the password.
 * Return      : ROOM_OK, ROOM_NOT_FOUND, ROOM_BAD_PASSWORD or
 *               ROOM_ALREADY_ONLINE (replies sent only on success)
 */
RoomStatus room_login(ChatRoom *room, const char *username, const char *password, int fd)
{
    UserRecord *user;
    RoomStatus status = ROOM_OK;

    if (!proto_valid_username(username))
        return ROOM_NOT_FOUND;

    pthread_mutex_lock(&room->lock);
    user = user_db_find(&room->db, username);
    if (user == NULL)
        status = ROOM_NOT_FOUND;
    else if (!user_db_password_matches(user, password))
        status = ROOM_BAD_PASSWORD;
    else if (is_online(user))
        status = ROOM_ALREADY_ONLINE;
    else
        go_online_locked(room, user, fd, REPLY_LOGIN_OK);
    pthread_mutex_unlock(&room->lock);
    return status;
}

/*
 * Function    : room_logout
 * Description : Logout request (spec 3.5 / 3.6). Clears the user's online
 *               entry, confirms to the user and tells the others. The
 *               registration itself is kept. Always sends a reply.
 * Return      : ROOM_OK or ROOM_NOT_LOGGED_IN
 */
RoomStatus room_logout(ChatRoom *room, const char *username, int fd)
{
    UserRecord *user;
    RoomStatus status = ROOM_OK;

    pthread_mutex_lock(&room->lock);
    user = user_db_find(&room->db, username);
    if (user == NULL || !is_online(user) || user->sockfd != fd)
    {
        status = ROOM_NOT_LOGGED_IN;
        check_sent(proto_send_reply(fd, REPLY_ERROR, REPLY_LOGOUT_FAILED), fd);
    }
    else
    {
        check_sent(proto_send_reply(fd, REPLY_OK, REPLY_LOGOUT_OK), fd);
        go_offline_locked(room, user);
    }
    pthread_mutex_unlock(&room->lock);
    return status;
}

/*
 * Function    : room_disconnect
 * Description : The session's connection ended without a logout. Marks the
 *               user offline (before the caller closes the socket) and
 *               tells the others.
 * Return      : 1 if the user was online on this socket, else 0
 */
int room_disconnect(ChatRoom *room, const char *username, int fd)
{
    UserRecord *user;
    int was_online = 0;

    pthread_mutex_lock(&room->lock);
    user = user_db_find(&room->db, username);
    if (user != NULL && is_online(user) && user->sockfd == fd)
    {
        go_offline_locked(room, user);
        was_online = 1;
    }
    pthread_mutex_unlock(&room->lock);
    return was_online;
}

/*
 * Function    : route_private_locked
 * Description : Checks a private message and delivers it to the receiver.
 *               The sender field is always the logged-in name of the
 *               session, so users cannot pretend to be someone else.
 * Return      : ROOM_OK or the reason it was not delivered
 */
static RoomStatus route_private_locked(ChatRoom *room, const char *sender,
                                       const char *receiver, const char *text)
{
    UserRecord *target;

    if (proto_text_is_blank(text))
        return ROOM_EMPTY_MESSAGE;
    if (strcmp(sender, receiver) == 0)
        return ROOM_SELF_MESSAGE;
    target = proto_valid_username(receiver) ? user_db_find(&room->db, receiver) : NULL;
    if (target == NULL)
        return ROOM_NO_RECEIVER;
    if (!is_online(target))
        return ROOM_RECEIVER_OFFLINE;
    if (check_sent(proto_send_chat(target->sockfd, sender, receiver, text, CHAT_SINGLE),
                   target->sockfd) != 0)
    {
        log_info("Could not deliver to '%s', closing that connection", receiver);
        return ROOM_NOT_DELIVERED;
    }
    return ROOM_OK;
}

/*
 * Function    : room_send_private
 * Description : Single chat: sends one message to one online user and
 *               replies to the sender with the result
 * Return      : ROOM_OK or the reason it was not delivered
 */
RoomStatus room_send_private(ChatRoom *room, const char *sender, int fd,
                             const char *receiver, const char *text)
{
    RoomStatus status;

    pthread_mutex_lock(&room->lock);
    status = route_private_locked(room, sender, receiver, text);
    check_sent(proto_send_reply(fd, status == ROOM_OK ? REPLY_OK : REPLY_ERROR,
                                status == ROOM_OK ? REPLY_SENT : room_status_text(status)), fd);
    pthread_mutex_unlock(&room->lock);
    return status;
}

/*
 * Function    : room_send_group
 * Description : Group chat (spec 2.1.10): sends the message to every other
 *               online user and replies to the sender with the result
 * Return      : ROOM_OK (with *delivered set), ROOM_EMPTY_MESSAGE,
 *               ROOM_NOBODY_ONLINE or ROOM_NOT_DELIVERED
 */
RoomStatus room_send_group(ChatRoom *room, const char *sender, int fd,
                           const char *text, size_t *delivered)
{
    RoomStatus status = ROOM_OK;
    size_t i, recipients = 0;

    *delivered = 0;
    pthread_mutex_lock(&room->lock);
    if (proto_text_is_blank(text))
    {
        status = ROOM_EMPTY_MESSAGE;
    }
    else
    {
        for (i = 0; i < room->db.count; i++)
        {
            UserRecord *user = &room->db.users[i];

            if (!is_online(user) || strcmp(user->username, sender) == 0)
                continue;
            recipients++;
            if (check_sent(proto_send_chat(user->sockfd, sender, "", text, CHAT_GROUP),
                           user->sockfd) == 0)
                (*delivered)++;
            else
                log_info("Could not deliver to '%s', closing that connection", user->username);
        }
        if (recipients == 0)
            status = ROOM_NOBODY_ONLINE;
        else if (*delivered == 0)
            status = ROOM_NOT_DELIVERED;
    }
    check_sent(proto_send_reply(fd, status == ROOM_OK ? REPLY_OK : REPLY_ERROR,
                                status == ROOM_OK ? REPLY_SENT : room_status_text(status)), fd);
    pthread_mutex_unlock(&room->lock);
    return status;
}

/*
 * Function    : room_send_list
 * Description : Answers a list request with all other online users
 */
void room_send_list(ChatRoom *room, const char *username, int fd)
{
    pthread_mutex_lock(&room->lock);
    send_list_locked(room, username, fd);
    pthread_mutex_unlock(&room->lock);
}

/*
 * Function    : room_reply
 * Description : Sends a reply to one socket under the room lock
 */
void room_reply(ChatRoom *room, int fd, uint32_t code, const char *text)
{
    pthread_mutex_lock(&room->lock);
    check_sent(proto_send_reply(fd, code, text), fd);
    pthread_mutex_unlock(&room->lock);
}

/*
 * Function    : room_status_text
 * Description : Reply text that explains a failed operation
 */
const char *room_status_text(RoomStatus status)
{
    switch (status)
    {
    case ROOM_DUPLICATE:          return REPLY_DUPLICATE;
    case ROOM_NOT_FOUND:          return REPLY_NOT_FOUND;
    case ROOM_BAD_PASSWORD:       return REPLY_BAD_PASSWORD;
    case ROOM_ALREADY_ONLINE:     return REPLY_ALREADY_ONLINE;
    case ROOM_INVALID_USERNAME:   return REPLY_INVALID_USERNAME;
    case ROOM_INVALID_PASSWORD:   return REPLY_INVALID_PASSWORD;
    case ROOM_DB_FULL:            return REPLY_DB_FULL;
    case ROOM_DB_ERROR:           return REPLY_DB_ERROR;
    case ROOM_NOT_LOGGED_IN:      return REPLY_LOGOUT_FAILED;
    case ROOM_NO_RECEIVER:        return REPLY_NO_RECEIVER;
    case ROOM_RECEIVER_OFFLINE:   return REPLY_RECEIVER_OFFLINE;
    case ROOM_SELF_MESSAGE:       return REPLY_SELF_MESSAGE;
    case ROOM_EMPTY_MESSAGE:      return REPLY_EMPTY_MESSAGE;
    case ROOM_NOBODY_ONLINE:      return REPLY_NOBODY_ONLINE;
    case ROOM_NOT_DELIVERED:      return REPLY_NOT_DELIVERED;
    case ROOM_OK:
    default:                      return REPLY_SENT;
    }
}
