/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : user_db.c
 *           Descriptions   : User database: a growable array of UserRecord in memory, backed
 *                            by a text file with one "username password" line per registered
 *                            user. New users are appended to the file.
 *           Objective      : Keep registrations across server restarts (spec 2.1.4) and hold
 *                            the online status and socket of every user (spec 3.7)
 *           Usage          : Used by chat_room.c through user_db.h. Not thread-safe by itself:
 *                            chat_room.c calls it only while holding its mutex.
 *           Output         : The database file (created with mode 0600)
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* pread(), strtok_r() */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "user_db.h"

#define FIELD_SEPARATORS   " \t\r\n"
#define GROWTH_FACTOR      2

/*
 * Function    : reserve
 * Description : Makes room for at least 'needed' records
 * Return      : USER_DB_OK or USER_DB_ERR_NOMEM
 */
static int reserve(UserDb *db, size_t needed)
{
    UserRecord *grown;
    size_t capacity;

    if (needed <= db->capacity)
        return USER_DB_OK;
    capacity = (db->capacity == 0) ? USER_DB_INITIAL_CAPACITY : db->capacity;
    while (capacity < needed)
        capacity *= GROWTH_FACTOR;
    grown = realloc(db->users, capacity * sizeof *grown);
    if (grown == NULL)
        return USER_DB_ERR_NOMEM;
    db->users = grown;
    db->capacity = capacity;
    return USER_DB_OK;
}

/*
 * Function    : store_record
 * Description : Appends a record to the array (space must be reserved).
 *               The record is zeroed first, so the fixed-size name and
 *               password fields are padded with NUL bytes.
 * Return      : The new record
 */
static UserRecord *store_record(UserDb *db, const char *username, const char *password)
{
    UserRecord *user = &db->users[db->count++];

    memset(user, 0, sizeof *user);
    proto_copy_text(user->username, sizeof user->username, username);
    proto_copy_text(user->password, sizeof user->password, password);
    user->status = STATUS_OFFLINE;
    user->sockfd = -1;
    return user;
}

/*
 * Function    : load_line
 * Description : Parses one "username password" line of the database file.
 *               Blank lines are ignored; invalid or duplicate entries are
 *               counted in *skipped and ignored.
 * Return      : USER_DB_OK or USER_DB_ERR_NOMEM
 */
static int load_line(UserDb *db, char *line, size_t *skipped)
{
    char *save = NULL;
    char *name = strtok_r(line, FIELD_SEPARATORS, &save);
    char *password, *extra;

    if (name == NULL)
        return USER_DB_OK;
    password = strtok_r(NULL, FIELD_SEPARATORS, &save);
    extra = strtok_r(NULL, FIELD_SEPARATORS, &save);
    if (password == NULL || extra != NULL || !proto_valid_username(name)
        || !proto_valid_password(password) || user_db_find(db, name) != NULL
        || db->count >= USER_DB_MAX_USERS)
    {
        (*skipped)++;
        return USER_DB_OK;
    }
    if (reserve(db, db->count + 1) != USER_DB_OK)
        return USER_DB_ERR_NOMEM;
    store_record(db, name, password);
    return USER_DB_OK;
}

/*
 * Function    : skip_rest_of_line
 * Description : Discards input up to and including the next newline
 */
static void skip_rest_of_line(FILE *fp)
{
    int c;

    do
    {
        c = fgetc(fp);
    } while (c != '\n' && c != EOF);
}

/*
 * Function    : user_db_load
 * Description : Initialises the database and loads every user from 'path'.
 *               A missing file is an empty database; it is created by the
 *               first registration.
 * Return      : USER_DB_OK, USER_DB_ERR_IO (errno set) or USER_DB_ERR_NOMEM
 */
int user_db_load(UserDb *db, const char *path, size_t *skipped)
{
    char line[USER_DB_LINE_SIZE];
    FILE *fp;
    int rc = USER_DB_OK, saved;

    memset(db, 0, sizeof *db);
    db->path = path;
    *skipped = 0;

    fp = fopen(path, "r");
    if (fp == NULL)
        return (errno == ENOENT) ? USER_DB_OK : USER_DB_ERR_IO;

    while (rc == USER_DB_OK && fgets(line, sizeof line, fp) != NULL)
    {
        size_t len = strlen(line);

        if (len > 0 && line[len - 1] != '\n' && !feof(fp))
        {
            skip_rest_of_line(fp);  /* Too long to be a valid entry */
            (*skipped)++;
            continue;
        }
        rc = load_line(db, line, skipped);
    }
    if (rc == USER_DB_OK && ferror(fp))
        rc = USER_DB_ERR_IO;
    saved = errno;
    fclose(fp);
    errno = saved;
    if (rc != USER_DB_OK)
        user_db_free(db);
    return rc;
}

/*
 * Function    : user_db_free
 * Description : Releases the memory of the database (the file is kept)
 */
void user_db_free(UserDb *db)
{
    free(db->users);
    db->users = NULL;
    db->count = 0;
    db->capacity = 0;
}

/*
 * Function    : user_db_find
 * Description : Looks a user up by exact (case-sensitive) name
 * Return      : The record, or NULL if there is no such user
 */
UserRecord *user_db_find(UserDb *db, const char *username)
{
    size_t i;

    for (i = 0; i < db->count; i++)
    {
        if (strcmp(db->users[i].username, username) == 0)
            return &db->users[i];
    }
    return NULL;
}

/*
 * Function    : write_all
 * Description : write() loop for partial writes and EINTR
 * Return      : 0 on success, -1 on error
 */
static int write_all(int fd, const char *buf, size_t len)
{
    while (len > 0)
    {
        ssize_t n = write(fd, buf, len);

        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

/*
 * Function    : append_line
 * Description : Appends "username password\n" to the database file,
 *               creating it with mode USER_DB_FILE_MODE. If the file does
 *               not end with a newline (edited by hand) one is added first.
 *               A failed write is undone by truncating to the old size.
 * Return      : 0 on success, -1 with errno set on error
 */
static int append_line(const char *path, const char *username, const char *password)
{
    char line[USER_DB_LINE_SIZE];
    struct stat st;
    char last = '\n';
    int fd, len, saved;

    fd = open(path, O_RDWR | O_APPEND | O_CREAT, USER_DB_FILE_MODE);
    if (fd < 0)
        return -1;
    if (fstat(fd, &st) != 0 || (st.st_size > 0 && pread(fd, &last, 1, st.st_size - 1) != 1))
    {
        saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    len = snprintf(line, sizeof line, "%s%s %s\n", (last == '\n') ? "" : "\n", username, password);
    if (len < 0 || (size_t)len >= sizeof line)
    {
        close(fd);
        errno = EINVAL;
        return -1;
    }
    if (write_all(fd, line, (size_t)len) != 0)
    {
        saved = errno;
        if (ftruncate(fd, st.st_size) != 0)
        {
            /* Nothing more can be done; the loader skips a broken last line */
        }
        close(fd);
        errno = saved;
        return -1;
    }
    return close(fd);
}

/*
 * Function    : user_db_add
 * Description : Registers a new user (the caller has checked that the name
 *               is valid and free). Memory is reserved before the file is
 *               written, so the file and the array always agree.
 * Return      : USER_DB_OK with *added set, USER_DB_ERR_FULL,
 *               USER_DB_ERR_NOMEM or USER_DB_ERR_IO (errno set)
 */
int user_db_add(UserDb *db, const char *username, const char *password, UserRecord **added)
{
    if (db->count >= USER_DB_MAX_USERS)
        return USER_DB_ERR_FULL;
    if (reserve(db, db->count + 1) != USER_DB_OK)
        return USER_DB_ERR_NOMEM;
    if (append_line(db->path, username, password) != 0)
        return USER_DB_ERR_IO;
    *added = store_record(db, username, password);
    return USER_DB_OK;
}

/*
 * Function    : user_db_password_matches
 * Description : Compares the whole fixed-size password field, so the time
 *               taken does not depend on where the first difference is
 * Return      : 1 if the password is correct, else 0
 */
int user_db_password_matches(const UserRecord *user, const char *password)
{
    char padded[PROTO_PASS_SIZE];
    unsigned char diff = 0;
    size_t i;

    if (strnlen(password, sizeof padded) >= sizeof padded)
        return 0;                   /* Longer than any stored password */
    proto_copy_text(padded, sizeof padded, password);
    for (i = 0; i < sizeof padded; i++)
        diff |= (unsigned char)(user->password[i] ^ padded[i]);
    return diff == 0;
}
