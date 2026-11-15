/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : user_db.h
 *           Descriptions   : The server's user database (spec 3.7: user name, password,
 *                            status, socket fd). Registrations are kept in a text file.
 *           Objective      : Store registered users permanently and track who is online
 *           Usage          : #include "user_db.h"  (not thread-safe: the caller locks)
 *           Output         : None (library module)
 *
###############################################################################################*/

#ifndef USER_DB_H
#define USER_DB_H

#include <stddef.h>
#include "protocol.h"

#define USER_DB_FILE              "users.db"   /* in the server's working directory */
#define USER_DB_FILE_MODE         0600         /* owner read / write only           */
#define USER_DB_INITIAL_CAPACITY  16
#define USER_DB_MAX_USERS         10000
#define USER_DB_LINE_SIZE         128          /* longest valid line is 64 bytes    */

/* Return codes */
#define USER_DB_OK                0
#define USER_DB_ERR_IO            -1           /* file error, errno is set          */
#define USER_DB_ERR_NOMEM         -2
#define USER_DB_ERR_FULL          -3

/* One user (spec 3.7). Status and sockfd live in memory only. */
typedef struct
{
    char username[PROTO_NAME_SIZE];
    char password[PROTO_PASS_SIZE];
    int status;                     /* STATUS_ONLINE or STATUS_OFFLINE          */
    int sockfd;                     /* socket of the logged-in session, else -1 */
} UserRecord;

typedef struct
{
    UserRecord *users;              /* registered users, in registration order */
    size_t count;
    size_t capacity;
    const char *path;               /* database file (not owned)               */
} UserDb;

int user_db_load(UserDb *db, const char *path, size_t *skipped);
void user_db_free(UserDb *db);
UserRecord *user_db_find(UserDb *db, const char *username);
int user_db_add(UserDb *db, const char *username, const char *password, UserRecord **added);
int user_db_password_matches(const UserRecord *user, const char *password);

#endif
