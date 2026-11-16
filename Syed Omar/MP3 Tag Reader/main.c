#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include"edit.h"
#include"view.h"
#include"type.h"

static void print_help(void)
{
    printf("***Help Menu***\n");
    printf("INFO:  Viewer: Use \"./mp3_tag_reader -v <filename>\"\n");
    printf("INFO:  Editor: Use \"./mp3_tag_reader -e [modifier] <new_content> <filename>\"\n\n");
    printf("INFO:  Modifiers:\n");
    printf("\t-t\tTitle\n\t-a\tArtist\n\t-A\tAlbum\n\t-y\tYear\n\t-m\tContent\n\t-c\tComment\n");
}

int main(int argc, char **argv)
{
    View viewInfo;
    Edit editInfo;

    if (argc < 2)
    {
        printf("ERROR:  Use \"./mp3_tag_reader --h\" for Help Menu\n");
        return failure;
    }

    Operation ret = check_operation_type(argv);

    if (ret == view)
    {
        printf("MP3 TAG READER AND EDITOR\n");
        printf("**************************\n");
        if (read_and_validate_mp3_file(argv, &viewInfo) != success)
            return failure;
        if (view_tag(argv, &viewInfo) != success)
            return failure;
        printf("\nINFO:  Done\n");
        printf("**************************\n");
    }
    else if (ret == edit)
    {
        printf("MP3 TAG READER AND EDITOR\n");
        printf("**************************\n");
        if (read_and_validate_mp3_file_args(argv, &editInfo) != success)
            return failure;
        if (edit_tag(argv, &editInfo) != success)
            return failure;
        printf("\nINFO:  Done\n");
        printf("**************************\n");
    }
    else if (ret == help)
    {
        print_help();
    }
    else
    {
        printf("ERROR:  Unsupported Operation. Use \"./mp3_tag_reader --h\" for Help Menu\n");
        return failure;
    }

    return success;
}
