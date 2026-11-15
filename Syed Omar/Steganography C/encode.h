#ifndef ENCODE_H
#define ENCODE_H

#include "types.h" // Contains user defined types

/* 
 * Structure to store information required for
 * encoding secret file to source Image
 * Info about output and intermediate data is
 * also stored
 */

#define MAX_SECRET_BUF_SIZE 1
#define MAX_IMAGE_BUF_SIZE (MAX_SECRET_BUF_SIZE * 8)
#define MAX_FILE_SUFFIX 4
#define HEADER_SIZE 54

typedef struct _EncodeInfo
{
    /* Source Image info */
    char *src_image_fname;      //store the src_image
    FILE *fptr_src_image;       //file pointer for src_image
    uint image_capacity;        //store the src_image filesize
    char image_data[MAX_IMAGE_BUF_SIZE];

    /* Secret File Info */
    char *secret_fname;         //store secret file
    FILE *fptr_secret;          //file pointer for secret file
    char extn_secret_file[MAX_FILE_SUFFIX];
    char secret_data[MAX_SECRET_BUF_SIZE];
    long size_secret_file;

    /* Stego Image Info */
    char *stego_image_fname;
    FILE *fptr_stego_image;

} EncodeInfo;


/* Encoding function prototype */

/* Check operation type */
OperationType check_operation_type(char *argv[]);

/* Read and validate Encode args from argv */
Status read_and_validate_encode_args(char *argv[], EncodeInfo *encodeInfo);

/* Perform the encoding */
Status do_encoding(EncodeInfo *encodeInfo);

/* Get File pointers for i/p and o/p files */
Status open_files(EncodeInfo *encodeInfo);

/* check capacity */
Status check_capacity(EncodeInfo *encodeInfo);

/* Get image size */
uint get_image_size_for_bmp(FILE *fptr_image);

/* Get file size */
uint get_file_size(FILE *fptr_secret);

/* Copy bmp image header */
Status copy_bmp_header(FILE *fptr_src_image, FILE *fptr_dest_image);

/* Store Magic String */
Status encode_magic_string(const char *magic_string, EncodeInfo *encodeInfo);

/* Encode function, which does the real encoding */
Status encode_data_to_image(const char *data, int size, FILE *fptr_src_image, FILE *fptr_stego_image);

/* Encode a byte into LSB of image data array */
Status encode_byte_to_lsb(char data, char *image_buffer);

/* Encode secret file extension size */
Status encode_secret_file_extn_size(int ext_size, EncodeInfo *encodeInfo);

/* Encode integer into LSB of image data array */
Status encode_int_to_lsb(int extn_size, char *image_buffer);

/* Encode secret file extension */
Status encode_secret_file_extn(const char *file_extn, EncodeInfo *encodeInfo);

/* Encode secret file size */
Status encode_secret_file_size(long file_size, EncodeInfo *encodeInfo);

/* Encode secret file data*/
Status encode_secret_file_data(EncodeInfo *encodeInfo);

/* Copy remaining image bytes from src to stego image after encoding */
Status copy_remaining_img_data(FILE *fptr_src, FILE *fptr_dest);

#endif
