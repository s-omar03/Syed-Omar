#ifndef VIEW_H
#define VIEW_H

#include<stdio.h>
#include "type.h"

#define FRAME_HEADER_SKIP 3L
#define FRAME_ID_SIZE     4
#define FRAME_ID_BUF_SIZE 5

typedef struct _View
{
    FILE *fptr_mp3;
    char frame_ID[FRAME_ID_BUF_SIZE];

    char *title_tag;
    uint title_tag_size;

    char *artist_tag;
    uint artist_tag_size;

    char *album_tag;
    uint album_tag_size;

    char *year_tag;
    uint year_size;

    char *content_tag;
    uint content_tag_size;

    char *comments_tag;
    uint comments_tag_size;
}View;

Operation check_operation_type(char *argv[]);
Status read_and_validate_mp3_file(char **argv, View *viewInfo);
Status view_tag(char **argv, View *viewInfo);
Status get_and_display(const char *str_frame, const char *str_id , uint *tag_size, char **tag, FILE *fp);
uint swap_endian(uint val);

#endif