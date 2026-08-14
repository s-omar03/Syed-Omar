#ifndef EDIT_H
#define EDIT_H

#include"type.h"

#define MAX_FRAME_SIZE 1024 * 1024

typedef struct _Edit
{
    FILE *fptr_mp3;
    FILE *fptr_temp;
    char frame_id[5];
    char frame_id_value[100];
    uint frame_id_size;
}Edit;

Status read_and_validate_mp3_file_args(char *argv[], Edit *editInfo);
Status edit_tag(char *argv[], Edit *editInfo);
Status copy_data(Edit *editInfo, int version);
Status copy_remaining_data(Edit *editInfo);
uint get_frame_size(char *header, int version);
void write_big_endian_uint(FILE *fp, uint value);

#endif