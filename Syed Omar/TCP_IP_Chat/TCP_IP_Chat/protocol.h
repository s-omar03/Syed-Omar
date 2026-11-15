/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : protocol.h
 *           Descriptions   : Wire protocol shared by the chat server and the chat client:
 *                            packet types, packet layouts (spec sections 3.1 - 3.6), reply
 *                            texts, input validation and the send / receive functions
 *           Objective      : Give both programs one exact, validated definition of every
 *                            message that travels over the TCP connection
 *           Usage          : #include "protocol.h"
 *           Output         : None (library module)
 *
###############################################################################################*/

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/* Server address defaults (spec 2.1.1: the server binds to port 6333) */
#define CHAT_DEFAULT_PORT        6333
#define CHAT_DEFAULT_PORT_TEXT   "6333"
#define CHAT_DEFAULT_SERVER_IP   "127.0.0.1"

/* Field sizes on the wire, including the terminating NUL byte */
#define PROTO_NAME_SIZE          32     /* user name: 1 to 31 characters      */
#define PROTO_PASS_SIZE          32     /* password: 1 to 31 characters       */
#define PROTO_TEXT_SIZE          512    /* chat text: up to 511 characters    */
#define PROTO_REPLY_TEXT_SIZE    128    /* server reply text                  */
#define PROTO_MAX_LIST_USERS     128    /* most names in one USER_LIST packet */
#define PROTO_U32_SIZE           4      /* every integer is a 32-bit value    */

/* Body sizes on the wire (header: type + length, both 32-bit) */
#define PROTO_HEADER_SIZE        (2 * PROTO_U32_SIZE)
#define PROTO_AUTH_BODY_SIZE     (PROTO_NAME_SIZE + PROTO_PASS_SIZE + PROTO_U32_SIZE)
#define PROTO_REPLY_BODY_SIZE    (PROTO_U32_SIZE + PROTO_REPLY_TEXT_SIZE)
#define PROTO_CHAT_BODY_SIZE     (2 * PROTO_NAME_SIZE + PROTO_TEXT_SIZE + PROTO_U32_SIZE)
#define PROTO_STATUS_BODY_SIZE   (PROTO_NAME_SIZE + PROTO_U32_SIZE)
#define PROTO_LIST_BODY_MAX      (PROTO_U32_SIZE + PROTO_MAX_LIST_USERS * PROTO_NAME_SIZE)
#define PROTO_MAX_BODY_SIZE      PROTO_LIST_BODY_MAX

/* Packet types (PacketHeader.type) */
#define PKT_AUTH_REQUEST         1      /* client -> server, spec 3.1             */
#define PKT_SERVER_REPLY         2      /* server -> client, spec 3.2 and 3.6     */
#define PKT_CHAT                 3      /* both directions, spec 3.3, 3.4 and 3.5 */
#define PKT_USER_STATUS          4      /* server -> client, join / leave notice  */
#define PKT_LIST_REQUEST         5      /* client -> server, empty body           */
#define PKT_USER_LIST            6      /* server -> client, online users         */

/* AuthRequest.option (spec 3.1 "Sign-up/sign-in") */
#define AUTH_REGISTER            1
#define AUTH_LOGIN               2

/* ChatMessage.chat_option (spec 3.3) */
#define CHAT_SINGLE              1
#define CHAT_GROUP               2
#define CHAT_LOGOUT              3

/* ServerReply.code */
#define REPLY_OK                 0      /* success                                  */
#define REPLY_ERROR              1      /* request refused, connection stays open   */
#define REPLY_FATAL              2      /* refused, the server closes the connection */

/* User status (StatusNotice.status and the server database) */
#define STATUS_OFFLINE           0
#define STATUS_ONLINE            1

/* Reply texts. The first ones are the exact texts of spec 3.2 and 3.6 */
#define REPLY_LOGIN_OK           "Logged in successfully"
#define REPLY_REGISTER_OK        "Registered successfully"
#define REPLY_DUPLICATE          "Duplicate user-name"
#define REPLY_NOT_FOUND          "user-name not found"
#define REPLY_BAD_PASSWORD       "Password not matching"
#define REPLY_LOGOUT_OK          "Logged out successfully"
#define REPLY_LOGOUT_FAILED      "Couldn't log out"
#define REPLY_WELCOME            "Welcome to the TCP/IP chat room"
#define REPLY_SERVER_FULL        "Server is full"
#define REPLY_ALREADY_ONLINE     "User is already logged in"
#define REPLY_TOO_MANY_FAILURES  "Too many failed attempts"
#define REPLY_INVALID_USERNAME   "Invalid user-name (use 1-31 letters, digits, '_', '.' or '-')"
#define REPLY_INVALID_PASSWORD   "Invalid password (use 1-31 printable characters, no spaces)"
#define REPLY_INVALID_OPTION     "Invalid option"
#define REPLY_DB_FULL            "User database is full"
#define REPLY_DB_ERROR           "Could not save the new user"
#define REPLY_LOGIN_FIRST        "Please log in first"
#define REPLY_ALREADY_LOGGED_IN  "Already logged in, log out first"
#define REPLY_SENT               "Message sent successfully"
#define REPLY_NO_RECEIVER        "Receiver not found"
#define REPLY_RECEIVER_OFFLINE   "Receiver is not online"
#define REPLY_NOBODY_ONLINE      "No other users are online"
#define REPLY_SELF_MESSAGE       "Cannot send a message to yourself"
#define REPLY_EMPTY_MESSAGE      "Message is empty"
#define REPLY_NOT_DELIVERED      "Message could not be delivered"
#define REPLY_INVALID_CHAT_OPT   "Invalid chat option"

/* Return codes of the send / receive functions */
#define PROTO_OK                 0
#define PROTO_CLOSED             -1     /* peer closed the connection between packets */
#define PROTO_ERR_IO             -2     /* socket error, timeout or cut-off packet     */
#define PROTO_ERR_BAD            -3     /* malformed packet                            */

/* Spec 3.1: login name, password, sign-up / sign-in option */
typedef struct
{
    char username[PROTO_NAME_SIZE];
    char password[PROTO_PASS_SIZE];
    uint32_t option;                        /* AUTH_REGISTER or AUTH_LOGIN       */
} AuthRequest;

/* Spec 3.2 and 3.6: error / success message */
typedef struct
{
    uint32_t code;                          /* REPLY_OK, REPLY_ERROR, REPLY_FATAL */
    char message[PROTO_REPLY_TEXT_SIZE];
} ServerReply;

/* Spec 3.3 - 3.5: sender, receiver, message, chat option */
typedef struct
{
    char sender[PROTO_NAME_SIZE];
    char receiver[PROTO_NAME_SIZE];         /* empty for group messages          */
    char message[PROTO_TEXT_SIZE];
    uint32_t chat_option;                   /* CHAT_SINGLE, CHAT_GROUP, CHAT_LOGOUT */
} ChatMessage;

/* Join / leave notice (spec 2.1.8) */
typedef struct
{
    char username[PROTO_NAME_SIZE];
    uint32_t status;                        /* STATUS_ONLINE or STATUS_OFFLINE   */
} StatusNotice;

/* List of online users (spec 2.1.7) */
typedef struct
{
    uint32_t total;
    char names[PROTO_MAX_LIST_USERS][PROTO_NAME_SIZE];
} UserList;

/* One received packet */
typedef struct
{
    uint32_t type;                          /* PKT_* value                       */
    union
    {
        AuthRequest auth;
        ServerReply reply;
        ChatMessage chat;
        StatusNotice status;
        UserList list;
    } body;
} Packet;

int proto_send_auth(int fd, const char *username, const char *password, uint32_t option);
int proto_send_reply(int fd, uint32_t code, const char *text);
int proto_send_chat(int fd, const char *sender, const char *receiver,
                    const char *text, uint32_t chat_option);
int proto_send_status(int fd, const char *username, uint32_t status);
int proto_send_list_request(int fd);
int proto_send_user_list(int fd, char names[][PROTO_NAME_SIZE], uint32_t count);

int proto_recv_packet(int fd, Packet *pkt);

int proto_valid_username(const char *name);
int proto_valid_password(const char *password);
int proto_text_is_blank(const char *text);
void proto_copy_text(char *dst, size_t size, const char *src);

#endif
