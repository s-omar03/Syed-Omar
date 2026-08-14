#include<stdio.h>
#include<string.h>
#include<stdlib.h>

#include"view.h"
#include"type.h"

Operation check_operation_type(char *argv[])
{
    if (!argv[1])
    {
        return unsupported;
    }

    if(strcmp(argv[1], "-v") == 0)
    {
        return view;
    }
    else if (strcmp(argv[1], "-e") == 0)
    {
        return edit;
    }
    else if (strcmp(argv[1], "--h") == 0)
    {
        return help;
    }
    else
    {
        return unsupported;
    }
}

Status read_and_validate_mp3_file(char **argv, View *viewInfo)
{
    if (argv[2] == NULL)
    {
        printf("INFO:  For viewing the tags -> ./mp3_tag_reader -v <filename>\n");
        return failure;
    }

    viewInfo->fptr_mp3 = fopen(argv[2], "rb");
    if (viewInfo->fptr_mp3 == NULL)
    {
        printf("ERROR: The file does not appear to be a valid MP3 (missing 'ID3' signature)\n");
        return failure;
    }

    char header[4] = {0};
    fread(header, 3, 1, viewInfo->fptr_mp3);
    if (strcmp(header, "ID3") != 0)
    {
        fprintf(stderr, "ERROR: Missing 'ID3' signature\n");
        fclose(viewInfo->fptr_mp3);
        viewInfo->fptr_mp3 = NULL;
        return failure;
    }

    printf("    Version ID: v2.3\n");
    printf("**************************\n\n");

    /* fptr_mp3 is deliberately left open here (positioned right after the
       3-byte "ID3" signature) so view_tag() can reuse it below instead of
       opening the same file a second time. */
    return success;
}

Status view_tag(char **argv, View *viewInfo)
{
    (void)argv; /* file is already open in viewInfo->fptr_mp3 */

    if (!viewInfo->fptr_mp3)
    {
        fprintf(stderr, "ERROR: MP3 file is not open\n");
        return failure;
    }

    /* Reuse the handle opened during validation instead of a second
       fopen() of the same file. */
    fseek (viewInfo->fptr_mp3, 10L, SEEK_SET);
    Status ret;

    ret = get_and_display("Title: ", "TIT2", &viewInfo->title_tag_size, &viewInfo->title_tag, viewInfo->fptr_mp3);
    if (ret == failure)
    {
        printf ("ERROR:  Title Frame ID Contents cannot be displayed.\n");
        fclose(viewInfo->fptr_mp3);
        viewInfo->fptr_mp3 = NULL;
        return failure;
    }

    ret = get_and_display("Artist: ", "TPE1", &viewInfo->artist_tag_size, &viewInfo->artist_tag, viewInfo->fptr_mp3);
    if (ret == failure)
    {
        printf ("ERROR:  Artist Frame ID Contents cannot be displayed.\n");
        fclose(viewInfo->fptr_mp3);
        viewInfo->fptr_mp3 = NULL;
        return failure;
    }

    ret = get_and_display("Album: ", "TALB", &viewInfo->album_tag_size, &viewInfo->album_tag, viewInfo->fptr_mp3);
    if (ret == failure)
    {
        printf ("ERROR:  Album Frame ID Contents cannot be displayed.\n");
        fclose(viewInfo->fptr_mp3);
        viewInfo->fptr_mp3 = NULL;
        return failure;
    }

    ret = get_and_display("Year: ", "TYER", &viewInfo->year_size, &viewInfo->year_tag, viewInfo->fptr_mp3);
    if (ret == failure)
    {
        printf ("ERROR:  Year Frame ID Contents cannot be displayed.\n");
        fclose(viewInfo->fptr_mp3);
        viewInfo->fptr_mp3 = NULL;
        return failure;
    }

    ret = get_and_display("Content: ", "TCON",  &viewInfo->content_tag_size, &viewInfo->content_tag, viewInfo->fptr_mp3);
    if (ret == failure)
    {
        printf ("ERROR:  Content Type Frame ID Contents cannot be displayed.\n");
        fclose(viewInfo->fptr_mp3);
        viewInfo->fptr_mp3 = NULL;
        return failure;
    }

    ret = get_and_display("Comments: ", "COMM", &viewInfo->comments_tag_size, &viewInfo->comments_tag, viewInfo->fptr_mp3);
    if (ret == failure)
    {
        printf ("ERROR:  Content Type Frame ID Contents cannot be displayed.\n");
        fclose(viewInfo->fptr_mp3);
        viewInfo->fptr_mp3 = NULL;
        return failure;
    }

    fclose (viewInfo->fptr_mp3);
    viewInfo->fptr_mp3 = NULL;

    return success;
}

Status get_and_display(const char *str_frame, const char *str_id, uint *tag_size, char **tag, FILE *fp)
{
    char frame_id[FRAME_ID_BUF_SIZE] = {0};
    uint size;

    while (fread(frame_id, 1, FRAME_ID_SIZE, fp) == FRAME_ID_SIZE)
    {
        frame_id[4] = '\0';

        if (strcmp(frame_id, "\0\0\0\0") == 0)
        {
            break;
        }

        if (fread(&size, sizeof(uint), 1, fp) != 1)
        {
            fprintf(stderr, "ERROR: Unable to read tag size\n");
            return failure;
        }

        size = swap_endian(size);

        fseek(fp, FRAME_HEADER_SKIP, SEEK_CUR);

        if (strcmp(frame_id, str_id) == 0)
        {
            *tag = (char *)malloc(size);
            if (!*tag)
            {
                fprintf(stderr, "ERROR: Memory allocation failed\n");
                return failure;
            }

            if (fread(*tag, 1, size - 1, fp) < size - 1)
            {
                fprintf(stderr, "ERROR: Unable to read tag data\n");
                free(*tag);
                return failure;
            }

            (*tag)[size - 1] = '\0';
            *tag_size = size;
            printf("%-10s%s\n", str_frame, *tag);
            free(*tag);
            return success;
        }
        else
        {
            /* Skip straight past the frame payload via fseek rather than
               reading it into a buffer we would immediately discard. */
            fseek(fp, size, SEEK_CUR);
        }
    }

    fprintf(stderr, "ERROR: %s Frame ID not found\n", str_id);
    return failure;
}

uint swap_endian(uint val)
{
    return ((val >> 24) & 0x000000FF) | ((val >> 8)  & 0x0000FF00) |
           ((val << 8)  & 0x00FF0000) | ((val << 24) & 0xFF000000);
}