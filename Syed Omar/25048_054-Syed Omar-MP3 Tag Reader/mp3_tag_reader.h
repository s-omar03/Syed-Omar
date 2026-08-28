#ifndef MP3_TAG_READER_H
#define MP3_TAG_READER_H

#include <stdio.h>

/* ---------- Operation types recognised on the command line ---------- */
typedef enum
{
    HELP,
    VIEW,
    EDIT,
    UNSUPPORTED
} OperationType;

/* ---------- Return codes used throughout the program ---------- */
typedef enum
{
    SUCCESS,
    FAILURE
} Status;

/* ---------- ID3v2.3 header (first 10 bytes of the file) ---------- */
typedef struct
{
    char tag[3];        /* "ID3" identifier                       */
    char version[2];     /* major, minor version                   */
    char flags;          /* header flags                           */
    char size[4];         /* syncsafe integer: total tag size       */
} ID3v2Header;

/* ---------- One ID3v2.3 frame header (10 bytes, before content) ---------- */
typedef struct
{
    char frame_id[5];    /* e.g. "TIT2", null terminated for printing */
    unsigned int size;   /* size of the frame content in bytes        */
    char flags[2];        /* frame flags                               */
} FrameHeader;

/* ---------- Everything the tool needs to know about one run ---------- */
typedef struct
{
    char *mp3_file;
    OperationType op;

    /* Only used for EDIT: which frame + what new value */
    char frame_id[5];
    char *new_value;
} TagInfo;

/* Frame IDs this tool understands, and their human readable labels */
typedef struct
{
    const char *cli_flag;   /* e.g. "-t"                */
    const char *frame_id;   /* e.g. "TIT2"               */
    const char *label;      /* e.g. "Title"              */
} FrameMap;

/* ------------------------------- API -------------------------------- */
Status check_args(int argc, char **argv, TagInfo *info);
Status view_tags(TagInfo *info);
Status edit_tag(TagInfo *info);
void display_help(void);

#endif /* MP3_TAG_READER_H */
