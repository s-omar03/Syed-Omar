/*
Functions Demonstrated:
1. Encoding a source file (beautiful.bmp) with secret file to stego file
2. Decoded secret file from stego file 

Sample input:
./a.out

Sample Output:
./a.out: Encoding: ./a.out -e <.bmp file> <.txt file> [output file]
./a.out: Decoding: ./a.out -d <.bmp file> [output file]
*/

#include <stdio.h>
#include<string.h>
#include<stdlib.h>

#include "encode.h"
#include "types.h"
#include "common.h"

/* Extension of a file name including the dot, or "" if there is none */
static const char *file_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot ? dot : "";
}

/* Function Definitions */

/* Check operation type */
OperationType check_operation_type(char *argv[])
{
    if(strcmp(argv[1], "-e") == 0)    //Status  = e_encode 
    {   
        return e_encode;
    }
    else if (strcmp(argv[1], "-d") == 0)   //Status = e_decode
    {
        return e_decode;
    }
    else
    {
        printf("INFO:  Unsupported Operator Type!!!\n");
        return e_unsupported;
    }
}

/* Read and validate Encode args from argv */
Status read_and_validate_encode_args(char *argv[], EncodeInfo *encodeInfo)
{
    /* Check if argv[2] is .bmp file */
    const char *src_ext = file_ext(argv[2]);
    if(strcmp(src_ext, ".bmp") == 0) 
    {    
        //Store the filename into structure
        encodeInfo -> src_image_fname = argv[2];
    }
    else
    {
        //Print the error
        printf("ERROR:  Source image file must have a .bmp extension!!!\n");
        return e_failure;
    }
    
    /* Check if argv[3] is .txt or .c or .sh file */
    const char *secret_ext = file_ext(argv[3]);
    if((strcmp(secret_ext, ".txt") == 0) || (strcmp(secret_ext, ".c") == 0) || (strcmp(secret_ext, ".sh") == 0)) 
    {
        //Store the filename and its extension (kept inside the image for decoding)
        encodeInfo -> secret_fname = argv[3];
        strncpy(encodeInfo->extn_secret_file, secret_ext, MAX_FILE_SUFFIX - 1);
        encodeInfo->extn_secret_file[MAX_FILE_SUFFIX - 1] = '\0';
    }
    else
    {
        //Print the error
        printf("ERROR:  Secret file must be .c, .sh, or .txt!!!\n");
        return e_failure;
    }
    
    /* If argv[4] is not mentioned in terminal, i.e, stego.bmp */ 
    if(argv[4] == NULL)     
    {
        //store the filename into the structure -> "stego.bmp"
        printf("INFO:  Output File not mentioned. Creating stego.bmp as default\n");
        encodeInfo -> stego_image_fname = "stego.bmp";
    }
    else
    {
        /* Check if argv[4] is .bmp file */
        const char *stego_ext = file_ext(argv[4]);
        if(strcmp(stego_ext, ".bmp") == 0)  
        {    
            //Store the filename into structure
            encodeInfo -> stego_image_fname = argv[4];
        }
        else
        {
            //Print the error
            printf("ERROR:  Stego image file must have a .bmp extension!!!\n");
            return e_failure;
        }
    }
    return e_success;
}

/* Perform the encoding */
Status do_encoding(EncodeInfo *encodeInfo)
{
    /* Open all files */
    printf("INFO:  Openning required files\n");
    if (open_files(encodeInfo) != e_success)
    {
        printf("ERROR:  Failed to open all files!!!\n");
        return e_failure;
    }
    printf("INFO:  Done\n");

    printf("INFO:  ## Encoding Procedure Started ##\n");
    
    /* Check if image have enough space to encode*/
    printf("INFO:  Checking for %s size\n", encodeInfo->secret_fname);
    if(check_capacity(encodeInfo) != e_success)
    {
        printf("ERROR:  Image doesn't have enough space to encode!!!\n");
        return e_failure;
    }
    printf("INFO:  Done. Found OK\n");

    /* Copy header of Source Image to Stego Image */
    printf("INFO:  Copying Image Header\n");
    if(copy_bmp_header(encodeInfo->fptr_src_image, encodeInfo->fptr_stego_image) != e_success)
    {
        printf("ERROR:  Failed to copy header of Source Image to Stego Image!!!\n");
        return e_failure;
    }
    printf("INFO:  Done\n");
    
    /* Store Magic String */
    printf("INFO:  Encoding Magic String Signature\n");
    if(encode_magic_string(MAGIC_STRING, encodeInfo) != e_success)
    {
        printf("ERROR:  Failed to store Magic String!!!\n");
        return e_failure;
    }
    printf("INFO:  Done\n");

    /* Encode secret file extension size */
    printf("INFO:  Encoding %s extension size\n", encodeInfo->secret_fname);
    if (encode_secret_file_extn_size(strlen(encodeInfo->extn_secret_file), encodeInfo) != e_success )      //extn_secret_file = 4 (.txt)
    {
        printf("ERROR:  Failed to encode secret file extension size!!!\n");
        return e_failure;
    }
    printf("INFO:  Done\n");

    /* Encode secret file extenstion */
    printf("INFO:  Encoding %s extension\n", encodeInfo->secret_fname);
    if (encode_secret_file_extn(encodeInfo->extn_secret_file, encodeInfo) != e_success)
    {
        printf("ERROR:  Failed to encode secret file extension!!!\n");
        return e_failure;
    }
    printf("INFO:  Done\n");

    /* Encode secret file size */
    printf("INFO:  Encoding %s file size\n", encodeInfo->secret_fname);
    if (encode_secret_file_size(encodeInfo->size_secret_file, encodeInfo) != e_success)
    {
        printf("ERROR:  Failed to encode secret file size");
    }
    printf("INFO:  Done\n");
    
    /* Encode secret file data*/
    printf("INFO:  Encoding %s file data\n", encodeInfo->secret_fname);
    if (encode_secret_file_data(encodeInfo) != e_success)
    {
        printf("ERROR:  Failed to encode secret file data!!!\n");
        return e_failure;
    }
    printf("INFO:  Done\n");

    /* Copy remaining image bytes from src to stego image after encoding */
    printf("INFO:  Copying Left Over Data\n");
    if (copy_remaining_img_data(encodeInfo->fptr_src_image, encodeInfo->fptr_stego_image) != e_success)
    {
        printf("ERROR:  Failed to copy remaining image data from source to stego!!!\n");
        return e_failure;
    }
    printf("INFO:  Done\n");


    return e_success;
}

/* 
 * Get File pointers for i/p and o/p files
 * Inputs: Src Image file, Secret file and
 * Stego Image file
 * Output: FILE pointer for above files
 * Return Value: e_success or e_failure, on file errors
 */
Status open_files(EncodeInfo *encodeInfo)
{
    // Src Image file
    encodeInfo->fptr_src_image = fopen(encodeInfo->src_image_fname, "r");

    // Do Error handling
    if (encodeInfo->fptr_src_image == NULL)
    {
    	perror("fopen");
    	fprintf(stderr, "ERROR:  Unable to open file %s\n", encodeInfo->src_image_fname);

    	return e_failure;
    }
    printf("INFO:  Opened %s\n", encodeInfo->src_image_fname);

    // Secret file
    encodeInfo->fptr_secret = fopen(encodeInfo->secret_fname, "r");
    // Do Error handling
    if (encodeInfo->fptr_secret == NULL)
    {
    	perror("fopen");
    	fprintf(stderr, "ERROR:  Unable to open file %s\n", encodeInfo->secret_fname);

    	return e_failure;
    }
    printf("INFO:  Opened %s\n", encodeInfo->secret_fname);

    // Stego Image file
    encodeInfo->fptr_stego_image = fopen(encodeInfo->stego_image_fname, "w");
    // Do Error handling
    if (encodeInfo->fptr_stego_image == NULL)
    {
    	perror("fopen");
    	fprintf(stderr, "ERROR:  Unable to open file %s\n", encodeInfo->stego_image_fname);

    	return e_failure;
    }
    printf("INFO:  Opened %s\n", encodeInfo->stego_image_fname);

    // No failure return e_success
    return e_success;
}

/* check capacity */
Status check_capacity(EncodeInfo *encodeInfo)
{
    //Storing source image size in "image capacity"
    encodeInfo->image_capacity = get_image_size_for_bmp(encodeInfo -> fptr_src_image);
    encodeInfo->size_secret_file =  get_file_size(encodeInfo ->fptr_secret);
    printf("INFO:  Done. Not Empty\n");
    //Storing header + other required encoding factors to "encoding_thing" 
    uint encoding_thing = 0; 
    encoding_thing = HEADER_SIZE + (8 * (strlen(MAGIC_STRING) + 4 + strlen(encodeInfo ->extn_secret_file) + encodeInfo->size_secret_file + 4));     //first 4 is ".txt", last 4 is data size of secret file

    printf("INFO:  Checking for %s capacity to handle %s\n", encodeInfo->src_image_fname, encodeInfo->secret_fname);
    if (encodeInfo->image_capacity < encoding_thing)
    {
        printf("ERROR:  Image Doesn't have enough capacity for encoding!!!\n");
        return e_failure;
    }
    
    return e_success;
}

/* Get image size
 * Input: Image file ptr
 * Output: width * height * bytes per pixel (3 in our case)
 * Description: In BMP Image, width is stored in offset 18,
 * and height after that. size is 4 bytes
 */
uint get_image_size_for_bmp(FILE *fptr_image)
{
    uint width, height;
    // Seek to 18th byte
    fseek(fptr_image, 18, SEEK_SET);

    // Read the width (an int)
    fread(&width, sizeof(int), 1, fptr_image);
    //printf("width = %u\n", width);

    // Read the height (an int)
    fread(&height, sizeof(int), 1, fptr_image);
    //printf("height = %u\n", height);

    // Return image capacity
    return width * height * 3;
}

/* Get file size */
uint get_file_size(FILE *fptr_secret)
{
    fseek(fptr_secret, 0, SEEK_END);
    return ftell(fptr_secret);
}

/* Copy bmp image header */
Status copy_bmp_header(FILE *fptr_src_image, FILE *fptr_dest_image)
{
    char header[HEADER_SIZE];

    //Make pointer come to begining of file, because our pointer is at 26th index while getting size of src_image file
    rewind(fptr_src_image);

    /* Read header from source file */
    if (fread(header, HEADER_SIZE, 1, fptr_src_image) != 1)
    {
        printf("ERROR:  Couldn't read BMP header properly!!!\n");
        return e_failure;
    }
    
    /* Write header to destination stego file */
    if (fwrite(header, HEADER_SIZE, 1, fptr_dest_image) != 1)
    {
        printf("ERROR:  Couldn't write BMP header to stego image properly!!!\n");
        return e_failure;
    }
    return e_success;
}

/* Store Magic String */
Status encode_magic_string(const char *magic_string, EncodeInfo *encodeInfo)
{
    /* Do magic string process*/
    if(encode_data_to_image(magic_string, strlen(magic_string), encodeInfo->fptr_src_image, encodeInfo->fptr_stego_image) != e_success)
    {
        return e_failure;
    }
    return e_success;
}

/* Encode function, which does the real encoding */
Status encode_data_to_image(const char *data, int size, FILE *fptr_src_image, FILE *fptr_stego_image)
{
    int i;
    char image_data[8];
    for (i = 0; i < size; i++)
    {
        // Read 8 bytes from src_image_file
        fread(image_data, 8, 1, fptr_src_image);
        
        encode_byte_to_lsb(data[i], image_data);
        
        // Write 8 byte to stego_image_file
        fwrite(image_data, 8, 1, fptr_stego_image);
    }
    return e_success;  
}

/* Encode a byte into LSB of image data array */
Status encode_byte_to_lsb(char data, char *image_buffer)
{
    int i, j = 7;
    for (i = 0; i < 8; i++)
    {
        // Get bit by bit of data from MSB side
        int bit = (data >> j) & 1;

        // Clear the LSB bit element in image_buffer and set the get bit
        image_buffer[i] = (image_buffer[i] & ~1) | bit;
        j--;
    }
    return e_success;   
}

/* Encode secret file extension size */
Status encode_secret_file_extn_size(int extn_size, EncodeInfo *encodeInfo)
{
    char buffer[32];
    
    // Read 32 bytes from source image 
    fread(buffer, 32, 1, encodeInfo->fptr_src_image);

    encode_int_to_lsb(extn_size, buffer);

    // Write 32 bytes to stego image
    fwrite(buffer, 32, 1, encodeInfo->fptr_stego_image);
    return e_success;
}

/* Encode integer into LSB of image data array */
Status encode_int_to_lsb(int extn_size, char *image_buffer)
{
    int i, j = 31;
    for (i = 0; i < 32; i++)
    {
        // Get bit by bit data from MSB
        int bit = (extn_size >> j) & 1;
        
        // Clear the LSB bit of element in image_buffer and set get bit
        image_buffer[i] = (image_buffer[i] & ~1) | bit;
        j--;
    }
    return e_success;
}

/* Encode secret file extenstion */
Status encode_secret_file_extn(const char *file_extn, EncodeInfo *encodeInfo)
{
    encode_data_to_image(file_extn, strlen(file_extn), encodeInfo->fptr_src_image, encodeInfo->fptr_stego_image);
    return e_success;
}

Status encode_secret_file_size(long file_size, EncodeInfo *encodeInfo)
{
    char secret[32];
    // Read next 32 bytes from source image
    fread(secret, 32, 1, encodeInfo->fptr_src_image);
    
    encode_int_to_lsb(file_size, secret);
    
    //Write 32 bytes to stego file
    fwrite(secret, 32, 1, encodeInfo->fptr_stego_image);

    return e_success;
}

/* Encode secret file data*/
Status encode_secret_file_data(EncodeInfo *encodeInfo)
{
    // Make the file pointer to indicate at start of file
    rewind(encodeInfo->fptr_secret);

    char file_data[encodeInfo->size_secret_file];

    // Read data from secret file
    fread(file_data, encodeInfo->size_secret_file, 1, encodeInfo->fptr_secret);
    
    // Encode file data to image
    encode_data_to_image(file_data, encodeInfo-> size_secret_file, encodeInfo->fptr_src_image, encodeInfo->fptr_stego_image);
    return e_success;
}

/* Copy remaining image bytes from src to stego image after encoding */
Status copy_remaining_img_data(FILE *fptr_src, FILE *fptr_stego)
{
    char ch;
    while (fread(&ch, 1, 1, fptr_src) > 0)
    {
        fwrite(&ch, 1, 1, fptr_stego);
    }
    return e_success;    
}