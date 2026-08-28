#include<stdio.h>
#include<string.h>
#include<stdlib.h>

#include"edit.h"
#include"view.h"
#include"type.h"

#define COPY_BUFFER_SIZE 8192

Status read_and_validate_mp3_file_args (char* argv[], Edit *editInfo)
{
    if (!argv[2] || !argv[3] || !argv[4])
    {
        printf("INFO:  For Editing -> ./mp3_tag_reader -e [modifier] <new_content> <filename>\n\n");
        printf("INFO:  Modifiers:\n");
        printf("\t-t\tTitle\n\t-a\tArtist\n\t-A\tAlbum\n\t-y\tYear\n\t-m\tContent\n\t-c\tComment\n");
        return failure;
    }

    if (strcmp(argv[2], "-t") == 0)
    {
        strncpy(editInfo->frame_id, "TIT2", 5);
    }
    else if (strcmp(argv[2], "-a") == 0)
    {
        strncpy(editInfo->frame_id, "TPE1", 5);
    }
    else if (strcmp(argv[2], "-A") == 0)
    {
        strncpy(editInfo->frame_id, "TALB", 5);
    }
    else if (strcmp(argv[2], "-y") == 0)
    {
        strncpy(editInfo->frame_id, "TYER", 5);
    }
    else if (strcmp(argv[2], "-m") == 0)
    {
        strncpy(editInfo->frame_id, "TCON", 5);
    }
    else if (strcmp(argv[2], "-c") == 0)
    {
        strncpy(editInfo->frame_id, "COMM", 5);
    }
    else
    {
        printf("ERROR:  Unsupported Modifier\n");
        return failure;
    }

    size_t value_len = strlen(argv[3]);
    if (value_len + 1 > sizeof(editInfo->frame_id_value))
    {
        printf("ERROR: Frame value too long\n");
        return failure;
    }

    editInfo->frame_id_value[0] = 0x00;
    /* memcpy instead of strncpy: length is already known exactly, so
       there is no need for strncpy's byte-by-byte length scan/padding. */
    memcpy(editInfo->frame_id_value + 1, argv[3], value_len);
    editInfo->frame_id_size = value_len + 1;

    /* Open the file once here and keep the handle open (rewound to the
       start) instead of opening it, closing it, and having edit_tag()
       open it again a moment later. */
    editInfo->fptr_mp3 = fopen(argv[4], "rb");
    if (!editInfo->fptr_mp3)
    {
        printf("ERROR: Cannot open file %s\n", argv[4]);
        return failure;
    }

    char sig[3];
    if (fread(sig, 1, 3, editInfo->fptr_mp3) != 3 || strncmp(sig, "ID3", 3) != 0)
    {
        printf("ERROR: Not a valid MP3 file\n");
        fclose(editInfo->fptr_mp3);
        editInfo->fptr_mp3 = NULL;
        return failure;
    }

    rewind(editInfo->fptr_mp3);
    return success;
}

const char* get_frame_description(const char* frame_id)
{
    if (strcmp(frame_id, "TIT2") == 0) return "Title";
    if (strcmp(frame_id, "TPE1") == 0) return "Artist";
    if (strcmp(frame_id, "TALB") == 0) return "Album";
    if (strcmp(frame_id, "TYER") == 0) return "Year";
    if (strcmp(frame_id, "COMM") == 0) return "Comment";
    if (strcmp(frame_id, "TCON") == 0) return "Content";
    return "Unknown";
}

Status edit_tag (char* argv[], Edit *editInfo)
{
    /* editInfo->fptr_mp3 is already open (from read_and_validate_mp3_file_args)
       and positioned at offset 0 - no second fopen() needed. */
    editInfo->fptr_temp = fopen("temp.mp3", "wb");

    if (editInfo->fptr_mp3 == NULL || editInfo->fptr_temp == NULL)
    {
        printf("ERROR: File open failed\n");
        if (editInfo->fptr_mp3) fclose(editInfo->fptr_mp3);
        if (editInfo->fptr_temp) fclose(editInfo->fptr_temp);
        return failure;
    }

    printf("INFO: Editing frame %s (%s) in file %s...\n", editInfo->frame_id, get_frame_description(editInfo->frame_id), argv[4]);

    char header[10];
    if (fread(header, 1, 10, editInfo->fptr_mp3) != 10)
    {
        printf("ERROR: Failed to read ID3 header\n");
        fclose(editInfo->fptr_mp3);
        fclose(editInfo->fptr_temp);
        remove("temp.mp3");
        return failure;
    }
    fwrite(header, 1, 10, editInfo->fptr_temp);
    printf("INFO:  ID3 header copied successfully\n");

    int id3_version = (unsigned char)header[3];

    printf("INFO:  Scanning frames\n");
    if (copy_data(editInfo, id3_version) == failure || copy_remaining_data(editInfo) == failure)
    {
        fclose(editInfo->fptr_mp3);
        fclose(editInfo->fptr_temp);
        remove("temp.mp3");
        return failure;
    }

    fclose(editInfo->fptr_mp3);
    fclose(editInfo->fptr_temp);

    if (remove(argv[4]) != 0 || rename("temp.mp3", argv[4]) != 0)
    {
        printf("ERROR: Failed to replace original file\n");
        return failure;
    }

    return success;
}

Status copy_data (Edit *editInfo, int version)
{
    char frame_header[10];
    char buffer[COPY_BUFFER_SIZE];

    while (fread(frame_header, 1, 10, editInfo->fptr_mp3) == 10)
    {
        uint frame_size = get_frame_size(frame_header, version);

        char frame_id[5];
        memcpy(frame_id, frame_header, 4);
        frame_id[4] = '\0';

        if (frame_id[0] == '\0' || strspn(frame_id, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") < 4)
        {
            printf("INFO:  Reached non-frame data or padding. Stopping frame scan.\n");
            fseek(editInfo->fptr_mp3, -10, SEEK_CUR);
            break;
        }

        if (frame_size == 0)
        {
            printf("INFO:  Encountered zero-size frame. Skipping.\n");
            continue;
        }

        if (frame_size > MAX_FRAME_SIZE)
        {
            printf("ERROR:  Frame size too large (%u bytes)\n", frame_size);
            return failure;
        }

        if (strcmp(frame_id, editInfo->frame_id) == 0)
        {
            printf("INFO:  Replacing frame %s\n", frame_id);
            fwrite(editInfo->frame_id, 1, 4, editInfo->fptr_temp);

            write_big_endian_uint(editInfo->fptr_temp, editInfo->frame_id_size);

            fwrite("\0\0", 1, 2, editInfo->fptr_temp);
            fwrite(editInfo->frame_id_value, 1, editInfo->frame_id_size, editInfo->fptr_temp);

            fseek(editInfo->fptr_mp3, frame_size, SEEK_CUR);
        }
        else
        {
            fwrite(frame_header, 1, 10, editInfo->fptr_temp);

            /* Copy this frame's payload in fixed-size chunks through a
               stack buffer instead of malloc()/free()'ing a
               frame_size-sized heap buffer for every single frame. */
            uint remaining = frame_size;
            while (remaining > 0)
            {
                size_t chunk = remaining < COPY_BUFFER_SIZE ? remaining : COPY_BUFFER_SIZE;
                size_t got = fread(buffer, 1, chunk, editInfo->fptr_mp3);
                if (got == 0)
                {
                    break;
                }
                fwrite(buffer, 1, got, editInfo->fptr_temp);
                remaining -= (uint)got;
            }
        }
    }

    return success;
}

Status copy_remaining_data (Edit *editInfo)
{
    char buffer[COPY_BUFFER_SIZE];
    size_t n;

    printf("INFO:  Copying remaining frames unchanged\n");
    printf("INFO:  Copying remaining MP3 data\n");

    /* Block copy instead of a 1-byte-at-a-time fread/fwrite loop. This is
       the single biggest win: the tail of an MP3 (the actual audio data)
       is typically megabytes, and byte-by-byte stdio calls dominate
       runtime there. */
    while ((n = fread(buffer, 1, sizeof(buffer), editInfo->fptr_mp3)) > 0)
    {
        fwrite(buffer, 1, n, editInfo->fptr_temp);
    }
    return success;
}

uint get_frame_size(char *header, int version)
{
    if (version == 4)
    {
        return ((header[4] & 0x7F) << 21) |
               ((header[5] & 0x7F) << 14) |
               ((header[6] & 0x7F) << 7)  |
               (header[7] & 0x7F);
    }
    else
    {
        return ((unsigned char)header[4] << 24) |
               ((unsigned char)header[5] << 16) |
               ((unsigned char)header[6] << 8)  |
               ((unsigned char)header[7]);
    }
}

void write_big_endian_uint(FILE *fp, uint value)
{
    fputc((value >> 24) & 0xFF, fp);
    fputc((value >> 16) & 0xFF, fp);
    fputc((value >> 8) & 0xFF, fp);
    fputc(value & 0xFF, fp);
}