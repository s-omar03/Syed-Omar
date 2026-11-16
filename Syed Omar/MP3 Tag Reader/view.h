#ifndef VIEW_H
#define VIEW_H

#include "type.h"

typedef struct _View
{
    FILE *fptr_mp3;     /* the MP3 file, opened for reading            */
    int   version;      /* ID3v2 major version (3 or 4)                 */
    uint  tag_end;      /* file offset where the ID3 tag ends           */
} View;

/* Decide whether the user asked for view, edit or help */
Operation check_operation_type(char *argv[]);

/* Check the file name and the ID3 header; opens the file */
Status read_and_validate_mp3_file(char *argv[], View *viewInfo);

/* Read the frames and print Title, Artist, Album, Year, Content, Comment */
Status view_tag(char *argv[], View *viewInfo);

#endif
