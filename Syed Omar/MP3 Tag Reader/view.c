#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "view.h"
#include "type.h"

#define MAX_TEXT_FRAME 65536   /* text frames larger than this are skipped */

/* Frames shown by the viewer. In ID3v2.4 the year frame is TDRC, not TYER. */
typedef struct
{
    const char *id;
    const char *id_v24;
    const char *label;
} TagEntry;

static const TagEntry entries[] =
{
    { "TIT2", "TIT2", "Title"   },
    { "TPE1", "TPE1", "Artist"  },
    { "TALB", "TALB", "Album"   },
    { "TYER", "TDRC", "Year"    },
    { "TCON", "TCON", "Content" },
    { "COMM", "COMM", "Comment" }
};
#define ENTRY_COUNT (sizeof(entries) / sizeof(entries[0]))

Operation check_operation_type(char *argv[])
{
    if (argv[1] == NULL)
        return unsupported;
    if (strcmp(argv[1], "-v") == 0)
        return view;
    if (strcmp(argv[1], "-e") == 0)
        return edit;
    if (strcmp(argv[1], "--h") == 0 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)
        return help;
    return unsupported;
}

Status read_and_validate_mp3_file(char *argv[], View *viewInfo)
{
    unsigned char header[10];
    const char *ext;

    if (argv[2] == NULL)
    {
        printf("INFO:  For viewing -> ./mp3_tag_reader -v <filename.mp3>\n");
        return failure;
    }

    ext = strrchr(argv[2], '.');
    if (ext == NULL || strcmp(ext, ".mp3") != 0)
    {
        printf("ERROR:  File must have a .mp3 extension\n");
        return failure;
    }

    viewInfo->fptr_mp3 = fopen(argv[2], "rb");
    if (viewInfo->fptr_mp3 == NULL)
    {
        perror("fopen");
        printf("ERROR:  Unable to open file %s\n", argv[2]);
        return failure;
    }

    if (fread(header, 1, 10, viewInfo->fptr_mp3) != 10 || memcmp(header, "ID3", 3) != 0)
    {
        printf("ERROR:  %s does not start with an ID3v2 tag\n", argv[2]);
        fclose(viewInfo->fptr_mp3);
        viewInfo->fptr_mp3 = NULL;
        return failure;
    }

    viewInfo->version = header[3];
    if (viewInfo->version != 3 && viewInfo->version != 4)
    {
        printf("ERROR:  Only ID3v2.3 and ID3v2.4 are supported (found v2.%d)\n", viewInfo->version);
        fclose(viewInfo->fptr_mp3);
        viewInfo->fptr_mp3 = NULL;
        return failure;
    }

    /* Tag size is a 28-bit "syncsafe" integer: 7 bits per byte */
    viewInfo->tag_end = 10 + (((uint)(header[6] & 0x7F) << 21) |
                              ((uint)(header[7] & 0x7F) << 14) |
                              ((uint)(header[8] & 0x7F) << 7)  |
                               (uint)(header[9] & 0x7F));

    /* Skip the extended header if the flag says there is one */
    if (header[5] & 0x40)
    {
        unsigned char ext_size[4];
        uint skip;
        if (fread(ext_size, 1, 4, viewInfo->fptr_mp3) != 4)
        {
            fclose(viewInfo->fptr_mp3);
            viewInfo->fptr_mp3 = NULL;
            return failure;
        }
        if (viewInfo->version == 4)
            skip = (((uint)(ext_size[0] & 0x7F) << 21) | ((uint)(ext_size[1] & 0x7F) << 14) |
                    ((uint)(ext_size[2] & 0x7F) << 7)  |  (uint)(ext_size[3] & 0x7F)) - 4;
        else
            skip = ((uint)ext_size[0] << 24) | ((uint)ext_size[1] << 16) |
                   ((uint)ext_size[2] << 8)  |  (uint)ext_size[3];
        fseek(viewInfo->fptr_mp3, (long)skip, SEEK_CUR);
    }

    printf("INFO:  ID3v2.%d tag found\n", viewInfo->version);
    return success;
}

static uint frame_size_from_header(const unsigned char *h, int version)
{
    if (version == 4)
        return ((uint)(h[4] & 0x7F) << 21) | ((uint)(h[5] & 0x7F) << 14) |
               ((uint)(h[6] & 0x7F) << 7)  |  (uint)(h[7] & 0x7F);
    return ((uint)h[4] << 24) | ((uint)h[5] << 16) | ((uint)h[6] << 8) | (uint)h[7];
}

/* Append one Unicode code point to out as UTF-8; returns the new length */
static size_t put_utf8(char *out, size_t len, unsigned cp)
{
    if (cp < 0x80)
        out[len++] = (char)cp;
    else if (cp < 0x800)
    {
        out[len++] = (char)(0xC0 | (cp >> 6));
        out[len++] = (char)(0x80 | (cp & 0x3F));
    }
    else
    {
        out[len++] = (char)(0xE0 | (cp >> 12));
        out[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[len++] = (char)(0x80 | (cp & 0x3F));
    }
    return len;
}

/* Convert the text of a frame (in its declared encoding) to a UTF-8 string.
   The caller frees the result. */
static char *text_to_utf8(const unsigned char *p, size_t n, int encoding)
{
    char *out = malloc(n * 3 + 1);
    size_t len = 0, i = 0;
    int big_endian = 0;

    if (out == NULL)
        return NULL;

    if (encoding == 0)                      /* ISO-8859-1 */
    {
        for (i = 0; i < n && p[i] != 0; i++)
            len = put_utf8(out, len, p[i]);
    }
    else if (encoding == 3)                 /* UTF-8 */
    {
        for (i = 0; i < n && p[i] != 0; i++)
            out[len++] = (char)p[i];
    }
    else                                    /* 1 = UTF-16 with BOM, 2 = UTF-16BE */
    {
        if (encoding == 2)
            big_endian = 1;
        if (encoding == 1 && n >= 2)
        {
            if (p[0] == 0xFE && p[1] == 0xFF) { big_endian = 1; i = 2; }
            else if (p[0] == 0xFF && p[1] == 0xFE) { i = 2; }
        }
        for (; i + 1 < n; i += 2)
        {
            unsigned cp = big_endian ? ((unsigned)p[i] << 8 | p[i + 1])
                                     : ((unsigned)p[i + 1] << 8 | p[i]);
            if (cp == 0)
                break;
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < n)       /* surrogate pair */
            {
                unsigned lo = big_endian ? ((unsigned)p[i + 2] << 8 | p[i + 3])
                                         : ((unsigned)p[i + 3] << 8 | p[i + 2]);
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
                out[len++] = (char)(0xF0 | (cp >> 18));
                out[len++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                out[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                out[len++] = (char)(0x80 | (cp & 0x3F));
                continue;
            }
            len = put_utf8(out, len, cp);
        }
    }
    out[len] = '\0';
    return out;
}

/* Turn the raw content of a frame into printable text */
static char *decode_frame(const char *frame_id, const unsigned char *data, size_t size)
{
    int encoding;
    size_t pos = 1;

    if (size < 1)
        return NULL;
    encoding = data[0];
    if (encoding > 3)
        return NULL;

    if (strcmp(frame_id, "COMM") == 0)
    {
        /* encoding(1) language(3) short description (terminated) text */
        pos = 4;
        if (pos > size)
            return NULL;
        if (encoding == 1 || encoding == 2)
        {
            while (pos + 1 < size && !(data[pos] == 0 && data[pos + 1] == 0))
                pos += 2;
            pos += 2;
        }
        else
        {
            while (pos < size && data[pos] != 0)
                pos++;
            pos++;
        }
        if (pos > size)
            pos = size;
    }
    return text_to_utf8(data + pos, size - pos, encoding);
}

Status view_tag(char *argv[], View *viewInfo)
{
    char *values[ENTRY_COUNT] = { 0 };
    unsigned char header[10];
    size_t i;
    (void)argv;

    while ((uint)ftell(viewInfo->fptr_mp3) + 10 <= viewInfo->tag_end)
    {
        uint size;
        char id[5];

        if (fread(header, 1, 10, viewInfo->fptr_mp3) != 10)
            break;
        if (header[0] == 0)                 /* padding: no more frames */
            break;

        memcpy(id, header, 4);
        id[4] = '\0';
        size = frame_size_from_header(header, viewInfo->version);

        for (i = 0; i < ENTRY_COUNT; i++)
        {
            const char *want = viewInfo->version == 4 ? entries[i].id_v24 : entries[i].id;
            if (strcmp(id, want) == 0 && values[i] == NULL && size > 0 && size <= MAX_TEXT_FRAME)
                break;
        }

        if (i < ENTRY_COUNT)
        {
            unsigned char *data = malloc(size);
            if (data == NULL || fread(data, 1, size, viewInfo->fptr_mp3) != size)
            {
                free(data);
                break;
            }
            values[i] = decode_frame(id, data, size);
            free(data);
        }
        else if (fseek(viewInfo->fptr_mp3, (long)size, SEEK_CUR) != 0)
            break;
    }

    printf("\n");
    for (i = 0; i < ENTRY_COUNT; i++)
        printf("%-8s: %s\n", entries[i].label, values[i] ? values[i] : "(not found)");

    for (i = 0; i < ENTRY_COUNT; i++)
        free(values[i]);
    fclose(viewInfo->fptr_mp3);
    viewInfo->fptr_mp3 = NULL;
    return success;
}
