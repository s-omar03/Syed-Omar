/*
Description :
Project 2 - Implement Steganography using C programing

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
#include "decode.h"
#include "types.h"
#include "common.h"

int main(int argc, char **argv)
{
    EncodeInfo encodeInfo;
    DecodeInfo decodeInfo;

    if (argc < 2)
    {
        printf("%s: Encoding: %s -e <.bmp file> <.txt file> [output file]\n", argv[0], argv[0]);
        printf("%s: Decoding: %s -d <.bmp file> [output file]\n", argv[0], argv[0]);
        return e_failure;
    }

    /* Check operation type */
    if(check_operation_type(argv) == e_encode)
    {
        if (argc < 3)
        {
            printf("%s: Encoding: %s %s <.bmp file> <.txt file> [output file]\n", argv[0], argv[0], argv[1]);
            return e_failure;
        }
        
        if (argc < 4)
        {
            if (strcmp(strchr(argv[2],'.'), ".bmp") == 0)
            {
                printf("%s: Encoding: %s %s %s <.txt file> [output file]\n", argv[0], argv[0], argv[1], argv[2]);   
            }
            else
            {
                printf("ERROR:  Source image file must have a .bmp extension!!!\n");
            }
            
            return e_failure;
        }
        
        if(argc >= 4 && argc <= 5)
        {
            /* Validate CLA */
            printf("*****Encoding*****\n");
            if(read_and_validate_encode_args(argv, &encodeInfo) == e_failure)
            {
                printf("ERROR:  Read and Validate encode arguments failed!!!\n");
                return e_failure;
            }
            printf("INFO:  Read and Validate encode arguments done...\n");

            //start the encoding
            if(do_encoding(&encodeInfo) == e_success)
            {
                printf("INFO:  ## Encoding Done Successfully ##\n");
            }
            else
            {
                printf("ERROR:  Encoding Failed!!!\n");
                return e_failure;
            }
        }
        else
        {
            //Print error
            perror("FAILURE:  ");
            return e_failure;
        }
        
    }
            
    
    else if (check_operation_type(argv) == e_decode)
    {
        if (argc < 3)
        {
            printf("%s: Decoding: %s %s <.bmp file> [output file]\n", argv[0], argv[0], argv[1]);
            return e_failure;
        }        
        
        if(argc >= 3 && argc <= 4)
        {
            /* Validate CLA */
            printf("*****Decoding*****\n");
            if(read_and_validate_decode_args(argv, &decodeInfo) == e_failure)
            {
                printf("ERROR:  Read and Validate decode arguments failed!!!\n");
                return e_failure;
            }
            printf("INFO:  Read and Validate decode arguments done...\n");

            //start the encoding
            if(do_decoding(&decodeInfo) == e_success)
            {
                printf("INFO:  ## Decoding Done Successfully ##\n");
            }
            else
            {
                printf("ERROR:  Decoding Failed!!!\n");
                return e_failure;
            }
        }
        else
        {
            //Print error
            perror("FAILURE:  ");
            return e_failure;
        }
    }

    else
    {
        printf("ERROR:  Unsupported Operation Type!!!\n");
        return e_failure;
    }
    
    
    printf("*****Project Success*****\n");
    return 0;
}
