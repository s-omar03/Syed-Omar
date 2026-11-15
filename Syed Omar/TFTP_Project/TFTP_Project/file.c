/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : file.c
 *           Descriptions   : All file operations: open, read a block, write a block, close,
 *                            with netascii conversion and path checks for the server
 *           Objective      : To read/write from/to file in both server and client side
 *           Usage          : file_open_read() / file_open_served() / file_create_temp(), then
 *                            file_read_block() or file_write_block(), then file_close() or
 *                            file_finish_write()
 *           Output         : None (library module); functions return -1 and set errno on error
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* openat(), O_NOFOLLOW, O_DIRECTORY */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "file.h"

#define PARENT_DIR  ".."
#define THIS_DIR    "."

/*
 * Function    : write_all
 * Description : Writes the whole buffer, retrying after signals and short writes
 * Return      : 0 on success, -1 with errno set on error
 */
static int write_all(int fd, const unsigned char *data, size_t len)
{
    while (len > 0)
    {
        ssize_t n = write(fd, data, len);

        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
        {
            errno = EIO;
            return -1;
        }
        data += n;
        len -= (size_t)n;
    }
    return 0;
}

/*
 * Function    : close_keep_errno
 * Description : Closes a descriptor on an error path without changing errno
 */
static void close_keep_errno(int fd)
{
    int saved = errno;

    if (fd >= 0)
        close(fd);
    errno = saved;
}

/*
 * Function    : file_attach
 * Description : Initialises a tftp_file_t around an open descriptor
 */
void file_attach(tftp_file_t *f, int fd, int netascii)
{
    f->fd = fd;
    f->netascii = netascii;
    netascii_encoder_init(&f->enc);
    netascii_decoder_init(&f->dec);
    f->buf_len = 0;
    f->buf_pos = 0;
    f->eof = 0;
    f->bytes = 0;
}

/*
 * Function    : open_regular
 * Description : Opens a file for reading relative to dir_fd and makes sure it
 *               is a regular file. O_NONBLOCK keeps a FIFO from blocking the
 *               open; it is switched off again for the reads.
 * Return      : Descriptor, or -1 with errno set (EISDIR for a directory,
 *               EACCES for other special files)
 */
static int open_regular(int dir_fd, const char *path, int extra_flags)
{
    struct stat st;
    int fd, flags;

    fd = openat(dir_fd, path, O_RDONLY | O_NONBLOCK | extra_flags);
    if (fd < 0)
        return -1;
    if (fstat(fd, &st) != 0)
    {
        close_keep_errno(fd);
        return -1;
    }
    if (!S_ISREG(st.st_mode))
    {
        close(fd);
        errno = S_ISDIR(st.st_mode) ? EISDIR : EACCES;
        return -1;
    }
    flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0)
    {
        close_keep_errno(fd);
        return -1;
    }
    return fd;
}

/*
 * Function    : file_open_read
 * Description : Client side: opens a local regular file for sending
 * Return      : 0 on success, -1 with errno set on error
 */
int file_open_read(tftp_file_t *f, const char *path, int netascii)
{
    int fd = open_regular(AT_FDCWD, path, 0);

    if (fd < 0)
        return -1;
    file_attach(f, fd, netascii);
    return 0;
}

/*
 * Function    : file_create_temp
 * Description : Client side: creates "<final_path>.<pid>.part" for a download.
 *               The caller renames it to final_path when the transfer succeeds
 *               and removes it otherwise, so a failed download never damages
 *               an existing file.
 * Return      : 0 on success, -1 with errno set on error
 */
int file_create_temp(tftp_file_t *f, const char *final_path, char *temp_path, size_t temp_size,
                     int netascii)
{
    int n, fd;

    n = snprintf(temp_path, temp_size, "%s.%ld%s", final_path, (long)getpid(), FILE_TEMP_SUFFIX);
    if (n < 0 || (size_t)n >= temp_size)
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    fd = open(temp_path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, FILE_LOCAL_MODE);
    if (fd < 0)
        return -1;
    file_attach(f, fd, netascii);
    return 0;
}

/*
 * Function    : file_check_name
 * Description : Server side: rejects names that could leave the served
 *               directory: empty names, absolute paths and any ".." component.
 *               (Symbolic links are refused later by file_open_served().)
 * Return      : 0 if the name is acceptable, -1 if not
 */
int file_check_name(const char *name)
{
    const char *p = name;

    if (name[0] == '\0' || name[0] == '/')
        return -1;
    for (;;)
    {
        const char *slash = strchr(p, '/');
        size_t len = (slash != NULL) ? (size_t)(slash - p) : strlen(p);

        if (len == strlen(PARENT_DIR) && strncmp(p, PARENT_DIR, len) == 0)
            return -1;
        if (slash == NULL)
            return 0;
        p = slash + 1;
    }
}

/*
 * Function    : is_symlink
 * Description : Tells whether 'name' in dir_fd is a symbolic link (errno is
 *               left unchanged)
 */
static int is_symlink(int dir_fd, const char *name)
{
    struct stat st;
    int saved = errno;
    int result = fstatat(dir_fd, name, &st, AT_SYMLINK_NOFOLLOW) == 0 && S_ISLNK(st.st_mode);

    errno = saved;
    return result;
}

/*
 * Function    : file_open_served
 * Description : Server side: opens 'name' below the directory root_fd. The
 *               path is walked one component at a time with openat() and
 *               O_NOFOLLOW, so neither ".." nor a symbolic link can lead
 *               outside the directory. Reading needs an existing regular file;
 *               writing creates a new file and fails with EEXIST if the name
 *               is already taken (uploads never overwrite).
 *               On success loc->dir_fd/leaf identify the file (to unlink a
 *               failed upload); release them with file_location_close().
 * Return      : File descriptor, or -1 with errno set
 */
int file_open_served(int root_fd, const char *name, int for_write, file_location_t *loc)
{
    char path[FILE_PATH_MAX + 1];
    char *component, *slash;
    size_t len = strlen(name);
    int dir_fd, fd;

    loc->dir_fd = -1;
    loc->leaf[0] = '\0';

    if (file_check_name(name) != 0)
    {
        errno = EACCES;
        return -1;
    }
    if (len > FILE_PATH_MAX)
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(path, name, len + 1);

    dir_fd = dup(root_fd);
    if (dir_fd < 0)
        return -1;

    /* Every component except the last must be a real directory */
    component = path;
    while ((slash = strchr(component, '/')) != NULL)
    {
        *slash = '\0';
        if (component[0] != '\0' && strcmp(component, THIS_DIR) != 0)
        {
            int sub = openat(dir_fd, component, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);

            if (sub < 0)
            {
                if (errno == ENOTDIR && is_symlink(dir_fd, component))
                    errno = ELOOP;      /* Linux reports ENOTDIR for a refused symlink */
                close_keep_errno(dir_fd);
                return -1;
            }
            close(dir_fd);
            dir_fd = sub;
        }
        component = slash + 1;
    }

    if (component[0] == '\0' || strcmp(component, THIS_DIR) == 0)
    {
        close(dir_fd);
        errno = EISDIR;
        return -1;
    }
    if (strlen(component) > FILE_LEAF_MAX)
    {
        close(dir_fd);
        errno = ENAMETOOLONG;
        return -1;
    }

    if (for_write)
        fd = openat(dir_fd, component, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, FILE_SERVER_MODE);
    else
        fd = open_regular(dir_fd, component, O_NOFOLLOW);
    if (fd < 0)
    {
        close_keep_errno(dir_fd);
        return -1;
    }

    loc->dir_fd = dir_fd;
    memcpy(loc->leaf, component, strlen(component) + 1);
    return fd;
}

/*
 * Function    : file_location_close
 * Description : Releases the directory descriptor kept by file_open_served()
 */
void file_location_close(file_location_t *loc)
{
    if (loc->dir_fd >= 0)
        close(loc->dir_fd);
    loc->dir_fd = -1;
}

/*
 * Function    : fill_buffer
 * Description : Refills the read buffer when it is empty
 * Return      : 1 if data is available, 0 at end of file, -1 on error
 */
static int fill_buffer(tftp_file_t *f)
{
    ssize_t n;

    if (f->buf_pos < f->buf_len)
        return 1;
    if (f->eof)
        return 0;
    do
    {
        n = read(f->fd, f->buf, sizeof f->buf);
    } while (n < 0 && errno == EINTR);

    if (n < 0)
        return -1;
    if (n == 0)
    {
        f->eof = 1;
        return 0;
    }
    f->buf_len = (size_t)n;
    f->buf_pos = 0;
    f->bytes += (unsigned long long)n;
    return 1;
}

/*
 * Function    : file_read_block
 * Description : Produces the payload of the next DATA packet: up to 'size'
 *               bytes in wire format (netascii-encoded if needed). Fewer
 *               than 'size' bytes are returned only at the end of the file,
 *               which is what marks the last block of a transfer.
 * Return      : Number of bytes stored in 'out', or -1 with errno set
 */
ssize_t file_read_block(tftp_file_t *f, unsigned char *out, size_t size)
{
    size_t used = 0;

    while (used < size)
    {
        int rc;

        if (f->netascii)
        {
            size_t consumed;

            used += netascii_encode(&f->enc, f->buf + f->buf_pos, f->buf_len - f->buf_pos,
                                    &consumed, out + used, size - used);
            f->buf_pos += consumed;
        }
        else if (f->buf_pos < f->buf_len)
        {
            size_t n = f->buf_len - f->buf_pos;

            if (n > size - used)
                n = size - used;
            memcpy(out + used, f->buf + f->buf_pos, n);
            f->buf_pos += n;
            used += n;
        }
        if (used == size)
            break;

        rc = fill_buffer(f);
        if (rc < 0)
            return -1;
        if (rc == 0)
            break;
    }
    return (ssize_t)used;
}

/*
 * Function    : file_write_block
 * Description : Stores the payload of a received DATA packet, decoding
 *               netascii first if needed
 * Return      : 0 on success, -1 with errno set (e.g. ENOSPC, EFBIG)
 */
int file_write_block(tftp_file_t *f, const unsigned char *data, size_t len)
{
    unsigned char out[NETASCII_DECODE_MAX(FILE_IO_BUFFER)];

    if (!f->netascii)
    {
        if (write_all(f->fd, data, len) != 0)
            return -1;
        f->bytes += len;
        return 0;
    }

    while (len > 0)
    {
        size_t chunk = (len > FILE_IO_BUFFER) ? FILE_IO_BUFFER : len;
        size_t n = netascii_decode(&f->dec, data, chunk, out);

        if (write_all(f->fd, out, n) != 0)
            return -1;
        f->bytes += n;
        data += chunk;
        len -= chunk;
    }
    return 0;
}

/*
 * Function    : file_finish_write
 * Description : Completes a received file: writes a CR left over by the
 *               netascii decoder, then closes the file and checks for errors
 *               reported by close()
 * Return      : 0 on success, -1 with errno set on error
 */
int file_finish_write(tftp_file_t *f)
{
    unsigned char tail[NETASCII_DECODE_MAX(0)];
    int rc = 0, saved = 0;

    if (f->netascii)
    {
        size_t n = netascii_decode_finish(&f->dec, tail);

        if (n > 0)
        {
            if (write_all(f->fd, tail, n) != 0)
            {
                rc = -1;
                saved = errno;
            }
            else
            {
                f->bytes += n;
            }
        }
    }
    if (f->fd >= 0)
    {
        if (close(f->fd) != 0 && errno != EINTR && rc == 0)
        {
            rc = -1;
            saved = errno;
        }
        f->fd = -1;
    }
    if (rc != 0)
        errno = saved;
    return rc;
}

/*
 * Function    : file_close
 * Description : Closes the file (if still open) without reporting errors;
 *               used for files that were only read and on error paths
 */
void file_close(tftp_file_t *f)
{
    if (f->fd >= 0)
        close_keep_errno(f->fd);
    f->fd = -1;
}
