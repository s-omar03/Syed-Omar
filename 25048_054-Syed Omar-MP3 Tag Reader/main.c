#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include"edit.h"
#include"view.h"
#include"type.h"
int main(int argc, char **argv)
{
    View viewInfo;
    Edit editInfo;
    if (argc < 2)
    {
        printf("ERROR:  Use \"./mp3_tag_reader --h\" for Help Menu\n ");
        return failure;
    }
    else
    {
        Operation ret = check_operation_type(argv);
        /* If return value is view*/
        if (ret == view)
        {
            printf("MP3 TAG READER AND EDITOR\n");
            printf("**************************\n");
            Status ret1 = read_and_validate_mp3_file(argv, &viewInfo);
            if (ret1 == success)
            {
                Status ret2 = view_tag(argv, &viewInfo);
                /* If view process is successfully executed */
                if (ret2 == success)
                {
                    printf("\nINFO:  Done\n");
                    printf("**************************\n");
                }
            }            
        }
        else if (ret == edit)
        {
            printf("MP3 TAG READER AND EDITOR\n");
            printf("**************************\n");
            Status ret1 = read_and_validate_mp3_file_args(argv, &editInfo);
            if (ret1 == success)
            {
                Status ret2 = edit_tag(argv, &editInfo);
                if (ret2 == success)
                {
                    printf("\nINFO:  Done\n");
                    printf("**************************\n");
                }
                
            }
            
        }
        else if(ret == help)
        {
            printf("***Help Menu***\n");
            printf("INFO:  Viewer: Use \"./mp3_tag_reader -v <filename>\n");
            printf("INFO:  Editor: Use \"./mp3_tag_reader -e [modifier] <new_content> <filename>\n\n");
            printf("INFO:  Modifiers:\n");
            printf("\t-t\tTitle\n\t-a\tArtist\n\t-A\tAlbum\n\t-y\tYear\n\t-m\tContent\n\t-c\tComment\n");
            return failure;           
        }
        
        else if (ret == unsupported)
        {
            printf("ERROR:  Unsupported Operation\n");
            return failure;
        }                 
    }
    
    return success;
}