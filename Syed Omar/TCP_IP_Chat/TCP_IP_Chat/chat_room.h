/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : chat_room.h
 *           Descriptions   : Shared state of the chat server: the user database plus one mutex,
 *                            and every operation that reads it or sends to a client socket
 *           Objective      : Register / log in / log out users, announce joins and leaves,
 *                            route private and group messages, all thread-safe
 *           Usage          : #include "chat_room.h"
 *           Output         : Packets to the connected clients
 *
###############################################################################################*/

#ifndef CHAT_ROOM_H
#define CHAT_ROOM_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include "user_db.h"

/* Result of a chat room operation */
typedef enum
{
    ROOM_OK = 0,
    ROOM_DUPLICATE,             /* register: name already taken             */
    ROOM_NOT_FOUND,             /* login: no such user                      */
    ROOM_BAD_PASSWORD,          /* login: wrong password                    */
    ROOM_ALREADY_ONLINE,        /* login: user logged in on another session */
    ROOM_INVALID_USERNAME,      /* register: name breaks the naming rules   */
    ROOM_INVALID_PASSWORD,      /* register: password breaks the rules      */
    ROOM_DB_FULL,               /* register: USER_DB_MAX_USERS reached      */
    ROOM_DB_ERROR,              /* register: users.db could not be written  */
    ROOM_NOT_LOGGED_IN,         /* logout: this session is not logged in    */
    ROOM_NO_RECEIVER,           /* private: no such user                    */
    ROOM_RECEIVER_OFFLINE,      /* private: receiver is not logged in       */
    ROOM_SELF_MESSAGE,          /* private: receiver is the sender          */
    ROOM_EMPTY_MESSAGE,         /* message text is empty or blank           */
    ROOM_NOBODY_ONLINE,         /* group: nobody else is logged in          */
    ROOM_NOT_DELIVERED          /* sending to the receiver(s) failed        */
} RoomStatus;

/*
 * The chat room. Locking rule: 'lock' protects 'db' AND every send to any
 * client socket, so packets for one socket are never interleaved and a
 * socket is never used after its session has marked the user offline.
 */
typedef struct
{
    UserDb db;
    pthread_mutex_t lock;
} ChatRoom;

int room_init(ChatRoom *room, const char *db_path, size_t *skipped);
void room_destroy(ChatRoom *room);
size_t room_user_count(ChatRoom *room);

RoomStatus room_register(ChatRoom *room, const char *username, const char *password, int fd);
RoomStatus room_login(ChatRoom *room, const char *username, const char *password, int fd);
RoomStatus room_logout(ChatRoom *room, const char *username, int fd);
int room_disconnect(ChatRoom *room, const char *username, int fd);

RoomStatus room_send_private(ChatRoom *room, const char *sender, int fd,
                             const char *receiver, const char *text);
RoomStatus room_send_group(ChatRoom *room, const char *sender, int fd,
                           const char *text, size_t *delivered);
void room_send_list(ChatRoom *room, const char *username, int fd);
void room_reply(ChatRoom *room, int fd, uint32_t code, const char *text);

const char *room_status_text(RoomStatus status);

#endif
