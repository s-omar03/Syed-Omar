#ifndef DECODE_H
#define DECODE_H

#include "encode.h"
#include "types.h"

typedef struct _DecodeInfo
{
    /* Secret File Info */
    char *secret_fname;         //Store secret file
    FILE *fptr_secret;          //File pointer for secret file
    uint secret_file_size;
    uint secret_file_extn_size;

    /* Stego Image Info */
    char *stego_image_fname;
    FILE *fptr_stego_image;
}DecodeInfo;

/* Decoding function prototype */

/* Read and validate Decode args from argv */
Status read_and_validate_decode_args(char *argv[], DecodeInfo *decodeInfo);

/* Perform the decoding */
Status do_decoding(DecodeInfo *decodeInfo);

/* Get File pointers for i/p file */
Status open_image_file(DecodeInfo *decodeInfo);

/* Skip bmp image header */
Status skip_bmp_header(FILE *fptr_stego_image);

/* Decode Magic String */
Status decode_magic_string(DecodeInfo *decodeInfo);

/* Decode secret file extension size */
Status decode_secret_file_extn_size(DecodeInfo *decodeInfo);

/* Decode integer from LSB of image data array */
long decode_int_from_lsb(char *image_buffer);

/* Decode secret file extension */
Status decode_secret_file_extn(DecodeInfo *decodeInfo);

/* Decode secret file size */
Status decode_secret_file_size(DecodeInfo *decodeInfo);

/* Decode secret file data*/
Status decode_secret_file_data(DecodeInfo *decodeInfo);

/* Decode a byte from LSB of image data array */
char decode_byte_from_lsb(char *image_buffer);

#endif