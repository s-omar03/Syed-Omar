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
    /* Payload layout: text frames = encoding byte + text.
       Comment frames = encoding byte + language(3) + empty description + text. */
    size_t prefix = (strcmp(editInfo->frame_id, "COMM") == 0) ? 5 : 1;
    if (prefix + value_len > sizeof(editInfo->frame_id_value))
    {
        printf("ERROR: Frame value too long (maximum %zu characters)\n",
               sizeof(editInfo->frame_id_value) - prefix);
        return failure;
    }

    editInfo->frame_id_value[0] = 0x00;                 /* encoding: ISO-8859-1 */
    if (prefix == 5)
    {
        memcpy(editInfo->frame_id_value + 1, "eng", 3);  /* language */
        editInfo->frame_id_value[4] = 0x00;              /* empty description */
    }
    memcpy(editInfo->frame_id_value + prefix, argv[3], value_len);
    editInfo->frame_id_size = (uint)(prefix + value_len);
    editInfo->size_delta = 0;
    editInfo->replaced = 0;
    editInfo->tag_end = 0;

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
    if (id3_version != 3 && id3_version != 4)
    {
        printf("ERROR: Only ID3v2.3 and ID3v2.4 are supported (found v2.%d)\n", id3_version);
        fclose(editInfo->fptr_mp3);
        fclose(editInfo->fptr_temp);
        remove("temp.mp3");
        return failure;
    }
    if (header[5] & 0x40)
    {
        printf("ERROR: Tags with an extended header are not supported\n");
        fclose(editInfo->fptr_mp3);
        fclose(editInfo->fptr_temp);
        remove("temp.mp3");
        return failure;
    }
    /* ID3v2.4 stores the year in TDRC; TYER only exists in v2.3 */
    if (id3_version == 4 && strcmp(editInfo->frame_id, "TYER") == 0)
        memcpy(editInfo->frame_id, "TDRC", 5);

    uint tag_size = ((uint)(header[6] & 0x7F) << 21) | ((uint)(header[7] & 0x7F) << 14) |
                    ((uint)(header[8] & 0x7F) << 7)  |  (uint)(header[9] & 0x7F);
    editInfo->tag_end = 10 + tag_size;

    printf("INFO:  Scanning frames\n");
    if (copy_data(editInfo, id3_version) == failure || copy_remaining_data(editInfo) == failure)
    {
        fclose(editInfo->fptr_mp3);
        fclose(editInfo->fptr_temp);
        remove("temp.mp3");
        return failure;
    }

    /* The tag size in the header must match the new frame sizes, otherwise
       players read the start of the audio as part of the tag (or the reverse). */
    if (editInfo->size_delta != 0)
    {
        long new_size = (long)tag_size + editInfo->size_delta;
        fflush(editInfo->fptr_temp);
        fseek(editInfo->fptr_temp, 6, SEEK_SET);
        write_syncsafe_uint(editInfo->fptr_temp, (uint)new_size);
    }

    fclose(editInfo->fptr_mp3);
    fclose(editInfo->fptr_temp);

    /* rename() replaces the original in one step, so a failure cannot lose it */
    if (rename("temp.mp3", argv[4]) != 0)
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

    /* Walk the frames, but never past the end of the ID3 tag */
    while ((uint)ftell(editInfo->fptr_mp3) + 10 <= editInfo->tag_end &&
           fread(frame_header, 1, 10, editInfo->fptr_mp3) == 10)
    {
        uint frame_size = get_frame_size(frame_header, version);

        char frame_id[5];
        memcpy(frame_id, frame_header, 4);
        frame_id[4] = '\0';

        if (frame_id[0] == '\0' || strspn(frame_id, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") < 4)
        {
            printf("INFO:  Reached padding. Stopping frame scan.\n");
            fseek(editInfo->fptr_mp3, -10, SEEK_CUR);
            break;
        }

        if (frame_size > MAX_FRAME_SIZE)
        {
            printf("ERROR:  Frame size too large (%u bytes)\n", frame_size);
            return failure;
        }

        if (!editInfo->replaced && strcmp(frame_id, editInfo->frame_id) == 0)
        {
            printf("INFO:  Replacing frame %s\n", frame_id);
            fwrite(editInfo->frame_id, 1, 4, editInfo->fptr_temp);
            write_frame_size(editInfo->fptr_temp, editInfo->frame_id_size, version);
            fwrite("\0\0", 1, 2, editInfo->fptr_temp);
            fwrite(editInfo->frame_id_value, 1, editInfo->frame_id_size, editInfo->fptr_temp);

            editInfo->size_delta += (long)editInfo->frame_id_size - (long)frame_size;
            editInfo->replaced = 1;
            fseek(editInfo->fptr_mp3, frame_size, SEEK_CUR);
        }
        else
        {
            fwrite(frame_header, 1, 10, editInfo->fptr_temp);

            /* Copy the payload in fixed-size chunks through a stack buffer */
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

    /* The frame was not in the file: add it after the last frame */
    if (!editInfo->replaced)
    {
        printf("INFO:  Frame %s not present. Adding it.\n", editInfo->frame_id);
        fwrite(editInfo->frame_id, 1, 4, editInfo->fptr_temp);
        write_frame_size(editInfo->fptr_temp, editInfo->frame_id_size, version);
        fwrite("\0\0", 1, 2, editInfo->fptr_temp);
        fwrite(editInfo->frame_id_value, 1, editInfo->frame_id_size, editInfo->fptr_temp);
        editInfo->size_delta += 10 + (long)editInfo->frame_id_size;
        editInfo->replaced = 1;
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

/* ID3v2.4 frame sizes are "syncsafe" (7 bits per byte); v2.3 uses plain big-endian */
void write_frame_size(FILE *fp, uint value, int version)
{
    if (version == 4)
        write_syncsafe_uint(fp, value);
    else
        write_big_endian_uint(fp, value);
}

void write_syncsafe_uint(FILE *fp, uint value)
{
    fputc((value >> 21) & 0x7F, fp);
    fputc((value >> 14) & 0x7F, fp);
    fputc((value >> 7) & 0x7F, fp);
    fputc(value & 0x7F, fp);
}
