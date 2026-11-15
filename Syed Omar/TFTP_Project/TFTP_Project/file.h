/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : file.h
 *           Descriptions   : File operations used by both programs: reading a file block by
 *                            block, writing received blocks, netascii conversion on the fly
 *                            and safe opening of files below the server's directory
 *           Objective      : Keep all file access (and its error handling) in one module
 *           Usage          : #include "file.h"
 *           Output         : None (library module)
 *
###############################################################################################*/

#ifndef TFTP_FILE_H
#define TFTP_FILE_H

#include <stddef.h>
#include <sys/types.h>
#include "netascii.h"

#define FILE_IO_BUFFER      8192    /* Bytes read from the disk at a time                */
#define FILE_LEAF_MAX       255     /* Longest single path component (NAME_MAX on Linux) */
#define FILE_PATH_MAX       512     /* Longest file name accepted from the network       */
#define FILE_SERVER_MODE    0644    /* Permissions of an uploaded file (before umask)    */
#define FILE_LOCAL_MODE     0666    /* Permissions of a downloaded file (before umask)   */
#define FILE_TEMP_SUFFIX    ".part" /* Download in progress: "<name>.<pid>.part"         */

/* An open file plus the netascii conversion state of the transfer */
typedef struct tftp_file
{
    int fd;                             /* -1 when closed                          */
    int netascii;                       /* 1: convert, 0: octet (raw bytes)        */
    netascii_encoder_t enc;             /* Reading: local text -> netascii         */
    netascii_decoder_t dec;             /* Writing: netascii -> local text         */
    unsigned char buf[FILE_IO_BUFFER];  /* Read buffer                             */
    size_t buf_len;                     /* Valid bytes in buf                      */
    size_t buf_pos;                     /* Next unused byte in buf                 */
    int eof;                            /* End of file reached while reading       */
    unsigned long long bytes;           /* Bytes read from / written to the disk   */
} tftp_file_t;

/* Where a file served by the server lives (to remove a failed upload) */
typedef struct file_location
{
    int dir_fd;                         /* Directory holding the file, -1 if none */
    char leaf[FILE_LEAF_MAX + 1];       /* Last component of the name             */
} file_location_t;

void file_attach(tftp_file_t *f, int fd, int netascii);
int file_open_read(tftp_file_t *f, const char *path, int netascii);
int file_create_temp(tftp_file_t *f, const char *final_path, char *temp_path, size_t temp_size,
                     int netascii);
int file_check_name(const char *name);
int file_open_served(int root_fd, const char *name, int for_write, file_location_t *loc);
void file_location_close(file_location_t *loc);
ssize_t file_read_block(tftp_file_t *f, unsigned char *out, size_t size);
int file_write_block(tftp_file_t *f, const unsigned char *data, size_t len);
int file_finish_write(tftp_file_t *f);
void file_close(tftp_file_t *f);

#endif
