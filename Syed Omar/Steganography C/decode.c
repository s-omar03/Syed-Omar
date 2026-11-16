

#include <stdio.h>
#include<string.h>
#include<stdlib.h>

#include "decode.h"
#include "types.h"
#include "common.h"

/* Extension of a file name including the dot, or "" if there is none */
static const char *file_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot ? dot : "";
}

/* Read and validate Decode args from argv */
Status read_and_validate_decode_args(char *argv[], DecodeInfo *decodeInfo)
{
    /* Check if argv[2] is .bmp file */
    const char *stego_ext = file_ext(argv[2]);
    if(strcmp(stego_ext, ".bmp") == 0) 
    {    
        //Store the filename into structure
        decodeInfo -> stego_image_fname = argv[2];
    }
    else
    {
        //Print the error
        printf("ERROR: Stego image file must have a .bmp extension!!!\n");
        return e_failure;
    }
    
    /* If argv[4] is not mentioned in terminal, i.e, secret(nestin) */ 
    if(argv[3] == NULL)     
    {
        //store the filename into the structure -> "secret(nestin)"
        decodeInfo->secret_fname = malloc(strlen("nestin") + 1);
        strcpy(decodeInfo->secret_fname, "nestin");
    }
    else
    {
        /* Check if argv[3] is having any extension */ 
        if(strchr(argv[3], '.') != NULL)  
        {   
            //Print the error
            printf("ERROR: Secret file must not have any extension!!!\n"); 
            return e_failure;
        }
        else
        {
            //Store the filename into structure
            decodeInfo->secret_fname = malloc(strlen(argv[3]) + 1);
            strcpy(decodeInfo->secret_fname,argv[3]);
        }
    }
    
    return e_success;
}

/* Perform the decoding */
Status do_decoding(DecodeInfo *decodeInfo)
{
    printf("INFO:  ## Decoding Procedure Started ##\n");

    //Open stego image
    printf("INFO:  Opening required files\n");
    if (open_image_file(decodeInfo) != e_success)
    {
        printf("ERROR:  Couldn't open stego image!!!\n");
        return e_failure;
    }
    printf("INFO:  Opened %s\n", decodeInfo->stego_image_fname);

    // Skip bmp image header
    printf("INFO:  Skip bmp header\n");
    if (skip_bmp_header(decodeInfo->fptr_stego_image) != e_success)
    {
        printf("ERROR:  Failed to skip header of Stego Image!!!\n");
        return e_failure;
    }
    printf("INFO:  Done\n");
    
    // Decode Magic String
    printf("INFO:  Decoding Magic String Signature\n");
    if (decode_magic_string(decodeInfo) != e_success)
    {
        printf("ERROR:  Failed to decode magic string!!!\n");
        return e_failure;
    }
    printf("INFO:  Done\n");

    // Decode secret file extension size
    printf("INFO:  Decoding secret file extension size\n");
    if (decode_secret_file_extn_size(decodeInfo) != e_success)
    {
        printf("ERROR:  Failed to encode secret file extension size!!!\n");
        return e_failure;
    }
    printf("INFO:  Done\n");
    
    // Decode secret file extension
    printf("INFO:  Decoding secret file extension and opening secret file...\n");
    if (decode_secret_file_extn(decodeInfo) != e_success)
    {
        printf("ERROR:  Failed to decode secret file extension!!!\n");
        return e_failure;
    }
    printf("INFO:  Done. Opened all required files\n");
    
    // Decode secret file size
    printf("INFO:  Decoding %s file size\n", decodeInfo->secret_fname);
    if (decode_secret_file_size(decodeInfo) != e_success)
    {
        printf("ERROR:  Failed to decode secret file size!!!\n");
        return e_failure;
    }
    printf("INFO:  Done\n");

    // Decode secret file data
    printf("INFO:  Decoding %s file data\n", decodeInfo->secret_fname);
    if (decode_secret_file_data(decodeInfo) != e_success)
    {
        printf("ERROR:  Failed to decode secret file data!!!\n");
        return e_failure;
    }
    printf("INFO:  Done\n");

    return e_success;
}

Status open_image_file(DecodeInfo *decodeInfo)
{
    //Open Stego file in read mode
    decodeInfo->fptr_stego_image = fopen(decodeInfo->stego_image_fname, "r");
    // Do Error handling
    if (decodeInfo->fptr_stego_image == NULL)
    {
    	perror("fopen");
    	fprintf(stderr, "ERROR: Unable to open file %s\n", decodeInfo->stego_image_fname);

    	return e_failure;
    }

    // No failure return e_success
    return e_success; 
}

/* Skip bmp image header */
Status skip_bmp_header(FILE *fptr_stego_image)
{
    // Move file pointer by 54 bytes to skip bmp header
    if (fseek(fptr_stego_image, HEADER_SIZE, SEEK_SET) != 0)
    {
        printf("ERROR:  Unable to skip bmp header!!!\n");
        return e_failure;
    }
    return e_success;
}

/* Decode Magic String */
Status decode_magic_string(DecodeInfo *decodeInfo)
{
    int i, size = strlen(MAGIC_STRING);
    char magic[8];
    char decoded_magic[size + 1];
    for (i = 0; i < size; i++)
    {
        //Read 8 bytes from stego.bmp
        fread(magic, 8, 1, decodeInfo->fptr_stego_image); 
        decoded_magic[i] = decode_byte_from_lsb(magic);
    }
    //Add ‘\0’ char after the loop
    decoded_magic[size] = '\0';

    //Compare decoded magic_string[] with original
    if (strcmp(decoded_magic, MAGIC_STRING) != 0)
    {
        printf("ERROR: Magic string mismatch!!!\n");
        return e_failure;
    }
        
    return e_success;
    
}

/* Decode a byte from LSB of image data array */
char decode_byte_from_lsb(char *image_buffer)
{
    int i, j = 7, get;
    char ch = 0x00;
    for (i = 0; i < 8; i++)
    {
        // Get lsb bit of an element of image buffer
        get = (image_buffer[i] & 1);

        //Add get bit in cleared position of ch
        ch = ch | (get << j);
        j--;
    }
    return ch;
}

/* Decode secret file extension size */
Status decode_secret_file_extn_size(DecodeInfo *decodeInfo)
{
    char buffer[32];

    //Read 32 bytes from stego
    fread(buffer, 32, 1, decodeInfo->fptr_stego_image);
    
    decodeInfo->secret_file_extn_size = decode_int_from_lsb(buffer);
    return e_success;
}

/* Decode integer from LSB of image data array */
long decode_int_from_lsb(char *image_buffer)
{
    // Logic to decode one byte 
    int i, j = 31, get;
    unsigned int val = 0;
    for (i = 0; i < 32; i++)
    {
        // Get LSB bit of an element of image_buffer 
        get = (image_buffer[i] & 1);
        val = val | ((unsigned int)get << j);
        j--;
    }
    return val;
}

/* Decode secret file extension */
Status decode_secret_file_extn(DecodeInfo *decodeInfo)
{
    int i, size = decodeInfo->secret_file_extn_size;
    char buffer[8], extn[16];

    /* The extension is stored with its dot (".txt", ".c", ".sh") */
    if (size < 0 || size > 7)
    {
        printf("ERROR:  Invalid secret file extension size (%d)!!!\n", size);
        return e_failure;
    }
    for (i = 0; i < size; i++)
    {
        //Read 8 bytes from stego
        if (fread(buffer, 8, 1, decodeInfo->fptr_stego_image) != 1)
        {
            printf("ERROR:  Stego image ended unexpectedly!!!\n");
            return e_failure;
        }
        extn[i] = decode_byte_from_lsb(buffer);
    }
    extn[i] = '\0';

    /* Images made by older versions stored no extension: use .txt for those */
    if (size == 0)
        strcpy(extn, ".txt");

    /* Build "<name><extension>" in a buffer that is big enough for both */
    char *full = malloc(strlen(decodeInfo->secret_fname) + strlen(extn) + 1);
    if (full == NULL)
    {
        printf("ERROR:  Out of memory!!!\n");
        return e_failure;
    }
    strcpy(full, decodeInfo->secret_fname);
    strcat(full, extn);
    free(decodeInfo->secret_fname);
    decodeInfo->secret_fname = full;

    //open secret file in write mode
    decodeInfo->fptr_secret = fopen(decodeInfo->secret_fname, "w");
    if (decodeInfo->fptr_secret == NULL)
    {
        perror("fopen");
        printf("ERROR:  Unable to create %s!!!\n", decodeInfo->secret_fname);
        return e_failure;
    }

    return e_success;
}

/* Decode secret file size */
Status decode_secret_file_size(DecodeInfo *decodeInfo)
{
    char buffer[32]; // 32 bytes, each holding 1 useful bit

    // Read 32 bytes from stego image
    fread(buffer, 1, 32, decodeInfo->fptr_stego_image);
    
    // Reconstruct the long value from LSBs and store returned value into structure member
    decodeInfo->secret_file_size = (long) decode_int_from_lsb(buffer);

    //printf("File size = %ld\n", decodeInfo->secret_file_size);

    return e_success;
}

/* Decode secret file data*/
Status decode_secret_file_data(DecodeInfo *decodeInfo)
{
    long i;
    char ch = 0;
    char secret[8];
    for (i = 0; i < decodeInfo->secret_file_size; i++)
    {
        // Read 8 bytes
        fread(secret, 8, 1, decodeInfo->fptr_stego_image);
        ch = decode_byte_from_lsb(secret);

        //write 1 byte into secret file
        fwrite(&ch, 1, 1, decodeInfo->fptr_secret);
    }
    
    return e_success;
}

