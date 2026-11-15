/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : index.c
 *           Descriptions   : Index builder (file parsing, word normalization, position
 *                            tracking) and persistence of the index to index.dat
 *           Objective      : Turn text documents into an inverted index that many threads can
 *                            build together, and store / reload it in a compact binary file
 *           Usage          : Used by main.c (build, save) and the search command (load)
 *           Output         : index.dat
 *
###############################################################################################*/

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "index.h"

#define INDEX_MAGIC        "IIDX"   /* First bytes of index.dat               */
#define INDEX_END_MAGIC    "IEND"   /* Last bytes, detects truncated files    */
#define INDEX_MAGIC_LEN    4
#define INDEX_VERSION      1        /* Bump when the file layout changes      */
#define INDEX_TMP_SUFFIX   ".tmp"   /* Index is written here, then renamed    */
#define VARINT_MAX_BYTES   10       /* 64-bit value, 7 bits per byte          */
#define VARINT_DATA_MASK   0x7F
#define VARINT_MORE_FLAG   0x80
#define FIRST_NON_ASCII    0x80     /* Bytes >= this are kept inside words    */
#define SAVE_CHUNKS_PER_THREAD 16   /* Work units per thread while saving     */

/*
 * Invariant used by index_save()
 * ------------------------------
 * In every word's occurrence list, the occurrences of one document form a
 * single contiguous run, in ascending position order. index_add_document()
 * guarantees it: a document is first indexed into a private table, each list
 * is put in ascending order, and then every list is spliced into the shared
 * table as one block (ht_merge). index_load() builds lists the same way.
 * Thanks to this the saver never has to sort individual occurrences.
 *
 * Document names
 * --------------
 * All names live back to back in one buffer (doc_names) in input order, and
 * FilePosition.filename points into it. Their addresses therefore increase
 * with the document id, which lets doc_id_of() map a filename pointer back to
 * its id with a binary search.
 *
 * Occurrence memory
 * -----------------
 * There is one FilePosition per word occurrence (millions for a large
 * collection), so they are not malloc'ed one by one. Each document takes
 * them from its own list of PositionBlocks (no locking needed), and when the
 * document is merged its blocks are handed to the index. Freeing the index
 * then costs one free() per block instead of one per occurrence.
 */

/* A document name before it is copied into the document table */
typedef struct
{
    const char *name;
    size_t len;
} NameRef;

/* Occurrences of one word inside one document, used while saving */
typedef struct
{
    size_t doc;                     /* Document id                       */
    const FilePosition *first;      /* First occurrence of the run       */
    size_t count;                   /* Number of occurrences in the run  */
} Run;

/* Words gathered from the table before sorting them */
typedef struct
{
    HashNode **nodes;
    size_t count;
    size_t capacity;
} NodeList;

/* Growable buffer that one part of index.dat is encoded into */
typedef struct
{
    unsigned char *data;
    size_t len;
    size_t capacity;
} ByteBuf;

/* Shared state of the threads encoding words while saving */
typedef struct
{
    const InvertedIndex *idx;
    HashNode **words;               /* Every word, in strcmp() order        */
    size_t word_count;
    ByteBuf *chunks;                /* Encoded output, one per chunk         */
    size_t chunk_count;
    size_t next_chunk;              /* Next chunk to encode (atomic counter) */
    int error;                      /* First error seen (atomic)             */
} SaveJob;

/* Bounds-checked cursor over the bytes of index.dat */
typedef struct
{
    const unsigned char *pos;
    const unsigned char *end;
    int bad;
} Reader;

/*******************************************************************************
 *                          Words and documents                                *
 *******************************************************************************/

/*
 * Function    : index_is_word_char
 * Description : A word is a run of ASCII letters/digits. Bytes >= 0x80 are
 *               accepted too so UTF-8 words are not cut into pieces.
 */
int index_is_word_char(int c)
{
    return isalnum(c) || c >= FIRST_NON_ASCII;
}

/*
 * Function    : set_documents
 * Description : Copies the names into one buffer and fills idx->docs
 */
static int set_documents(InvertedIndex *idx, const NameRef *refs, size_t count)
{
    size_t total = 0, i;
    char *p;

    for (i = 0; i < count; i++)
        total += refs[i].len + 1;

    idx->doc_names = malloc(total > 0 ? total : 1);
    idx->docs = malloc((count > 0 ? count : 1) * sizeof *idx->docs);
    if (idx->doc_names == NULL || idx->docs == NULL)
        return INDEX_ERR_NOMEM;

    p = idx->doc_names;
    for (i = 0; i < count; i++)
    {
        memcpy(p, refs[i].name, refs[i].len);
        p[refs[i].len] = '\0';
        idx->docs[i] = p;
        p += refs[i].len + 1;
    }
    idx->doc_count = count;
    return INDEX_OK;
}

/*
 * Function    : doc_id_of
 * Description : Maps a filename pointer taken from idx->docs back to its id
 * Return      : 0 and *id on success, -1 if the pointer is not a document name
 */
static int doc_id_of(const InvertedIndex *idx, const char *name, size_t *id)
{
    size_t low = 0, high = idx->doc_count;

    while (low < high)
    {
        size_t mid = low + (high - low) / 2;

        if (idx->docs[mid] == name)
        {
            *id = mid;
            return 0;
        }
        if (idx->docs[mid] < name)
            low = mid + 1;
        else
            high = mid;
    }
    return -1;
}

/*
 * Function    : new_position
 * Description : Takes one FilePosition from a block list, adding a block
 *               when the current one is full. Blocks start small and double
 *               in size, so small documents waste little memory.
 * Return      : The new entry, or NULL when out of memory
 */
static FilePosition *new_position(PositionBlock **blocks)
{
    PositionBlock *block = *blocks;

    if (block == NULL || block->used == block->capacity)
    {
        size_t capacity = INDEX_MIN_BLOCK_POSITIONS;

        if (block != NULL && block->capacity < INDEX_MAX_BLOCK_POSITIONS)
            capacity = block->capacity * 2;
        else if (block != NULL)
            capacity = INDEX_MAX_BLOCK_POSITIONS;

        block = malloc(sizeof *block + capacity * sizeof block->items[0]);
        if (block == NULL)
            return NULL;
        block->used = 0;
        block->capacity = capacity;
        block->next = *blocks;
        *blocks = block;
    }
    return &block->items[block->used++];
}

/*
 * Function    : free_blocks
 * Description : Frees a whole block list
 */
static void free_blocks(PositionBlock *block)
{
    while (block != NULL)
    {
        PositionBlock *next = block->next;

        free(block);
        block = next;
    }
}

/*
 * Function    : adopt_blocks
 * Description : Moves a document's block list into the index
 */
static void adopt_blocks(InvertedIndex *idx, PositionBlock **blocks)
{
    PositionBlock *last = *blocks;

    if (last == NULL)
        return;
    while (last->next != NULL)
        last = last->next;

    pthread_mutex_lock(&idx->blocks_lock);
    last->next = idx->blocks;
    idx->blocks = *blocks;
    pthread_mutex_unlock(&idx->blocks_lock);
    *blocks = NULL;
}

/*
 * Function    : index_alloc
 * Description : Allocates an empty InvertedIndex structure
 */
static InvertedIndex *index_alloc(void)
{
    InvertedIndex *idx = calloc(1, sizeof *idx);

    if (idx != NULL && pthread_mutex_init(&idx->blocks_lock, NULL) != 0)
    {
        free(idx);
        return NULL;
    }
    return idx;
}

/*
 * Function    : index_create
 * Description : Creates an empty index for the given files. Repeated file
 *               names are kept only once; idx->doc_count tells how many
 *               documents remain.
 * Return      : The index, or NULL when out of memory
 */
InvertedIndex *index_create(char *const files[], size_t count)
{
    InvertedIndex *idx = index_alloc();
    NameRef *refs = malloc((count > 0 ? count : 1) * sizeof *refs);
    HashTable *seen = ht_create(count * 2, 1);
    size_t unique = 0, i;

    if (idx == NULL || refs == NULL || seen == NULL)
        goto fail;

    for (i = 0; i < count; i++)
    {
        if (ht_lookup(seen, files[i]) != NULL)
            continue;                                   /* duplicate name */
        if (ht_insert(seen, files[i], NULL, 0) != 0)
            goto fail;
        refs[unique].name = files[i];
        refs[unique].len = strlen(files[i]);
        unique++;
    }

    if (set_documents(idx, refs, unique) != INDEX_OK)
        goto fail;
    idx->table = ht_create(INDEX_TABLE_CAPACITY, 0);
    if (idx->table == NULL)
        goto fail;

    ht_destroy(seen);
    free(refs);
    return idx;

fail:
    ht_destroy(seen);
    free(refs);
    index_free(idx);
    return NULL;
}

/*
 * Function    : index_free
 * Description : Releases the index and everything it owns
 */
void index_free(InvertedIndex *idx)
{
    if (idx == NULL)
        return;
    ht_destroy(idx->table);
    free_blocks(idx->blocks);
    free(idx->docs);
    free(idx->doc_names);
    pthread_mutex_destroy(&idx->blocks_lock);
    free(idx);
}

/*******************************************************************************
 *                              Index builder                                  *
 *******************************************************************************/

/*
 * Function    : reverse_positions
 * Description : ht_foreach() callback. Words are prepended while a document
 *               is scanned, so their lists end up newest first; reversing
 *               gives ascending positions.
 */
static void reverse_positions(HashNode *node, void *ctx)
{
    FilePosition *previous = NULL;
    FilePosition *current = node->positions;

    (void)ctx;
    while (current != NULL)
    {
        FilePosition *next = current->next;

        current->next = previous;
        previous = current;
        current = next;
    }
    node->positions = previous;
}

/*
 * Function    : add_occurrence
 * Description : Records one word occurrence in a document's private table
 */
static int add_occurrence(HashTable *table, PositionBlock **blocks,
                          const char *word, const char *name, long position)
{
    FilePosition *occurrence = new_position(blocks);

    if (occurrence == NULL)
        return INDEX_ERR_NOMEM;
    occurrence->filename = name;
    occurrence->position = position;
    occurrence->next = NULL;
    if (ht_insert_list(table, word, occurrence, occurrence) != 0)
        return INDEX_ERR_NOMEM;
    return INDEX_OK;
}

/*
 * Function    : index_add_document
 * Description : Indexes one document. Safe to call from several threads at
 *               once for different documents. The document is parsed into a
 *               private table first, then merged into the shared table, so a
 *               document that fails half way leaves no trace in the index.
 * Return      : INDEX_OK, INDEX_ERR_OPEN / INDEX_ERR_READ (errno is set) or
 *               INDEX_ERR_NOMEM
 */
int index_add_document(InvertedIndex *idx, size_t doc_id)
{
    const char *name = idx->docs[doc_id];
    char word[INDEX_MAX_WORD_LEN + 1];
    size_t word_len = 0;
    long word_start = 0;          /* Offset of the first character of 'word' */
    long offset = 0;              /* Offset of the byte being examined       */
    int in_word = 0;
    int rc = INDEX_OK;
    int saved_errno;
    unsigned char *buf = NULL;
    HashTable *local = NULL;
    PositionBlock *blocks = NULL;  /* Memory for this document's occurrences */
    FILE *fp;
    size_t n, i;

    fp = fopen(name, "rb");
    if (fp == NULL)
        return INDEX_ERR_OPEN;

    buf = malloc(INDEX_READ_CHUNK);
    local = ht_create(INDEX_DOC_TABLE_CAPACITY, 0);
    if (buf == NULL || local == NULL)
    {
        rc = INDEX_ERR_NOMEM;
        goto done;
    }

    while (rc == INDEX_OK && (n = fread(buf, 1, INDEX_READ_CHUNK, fp)) > 0)
    {
        for (i = 0; i < n; i++, offset++)
        {
            int c = buf[i];

            if (index_is_word_char(c))
            {
                if (!in_word)
                {
                    in_word = 1;
                    word_start = offset;
                    word_len = 0;
                }
                if (word_len < INDEX_MAX_WORD_LEN)
                    word[word_len++] = (char)tolower(c);
            }
            else if (in_word)
            {
                in_word = 0;
                word[word_len] = '\0';
                rc = add_occurrence(local, &blocks, word, name, word_start);
                if (rc != INDEX_OK)
                    break;
            }
        }
    }

    if (rc == INDEX_OK && ferror(fp))
        rc = INDEX_ERR_READ;

    /* The file may end in the middle of a word */
    if (rc == INDEX_OK && in_word)
    {
        word[word_len] = '\0';
        rc = add_occurrence(local, &blocks, word, name, word_start);
    }

    if (rc == INDEX_OK)
    {
        ht_foreach(local, reverse_positions, NULL);
        ht_merge(idx->table, local);
        adopt_blocks(idx, &blocks);
    }

done:
    saved_errno = errno;
    ht_destroy(local);
    free_blocks(blocks);           /* Only non-NULL if the document failed */
    free(buf);
    fclose(fp);
    errno = saved_errno;
    return rc;
}

/*******************************************************************************
 *                         Persistence (index.dat)                             *
 *******************************************************************************/

/*
 * index.dat layout (every integer is an unsigned LEB128 varint):
 *
 *   "IIDX"  version  doc_count  word_count
 *   doc_count  x { name_len  name_bytes }
 *   word_count x { word_len  word_bytes  run_count
 *                  run_count x { doc_id  occurrence_count
 *                                occurrence_count x position_delta } }
 *   "IEND"
 *
 * Words are stored in strcmp() order and runs in document order, so the same
 * input always gives a byte-identical file whatever the thread count was.
 * Positions are delta encoded inside a run (the first delta is the absolute
 * offset), which keeps most of them to one or two bytes.
 *
 * Saving walks every occurrence node, which is dominated by cache misses on
 * large indexes, so the words are split into chunks that several threads
 * encode into separate memory buffers; the buffers are then written in order.
 */

/*
 * Function    : buf_reserve
 * Description : Makes room for 'extra' more bytes in the buffer
 */
static int buf_reserve(ByteBuf *b, size_t extra)
{
    size_t capacity = b->capacity > 0 ? b->capacity : INDEX_IO_BUFFER;
    unsigned char *bigger;

    if (b->capacity - b->len >= extra)
        return INDEX_OK;
    while (capacity - b->len < extra)
    {
        if (capacity > SIZE_MAX / 2)
            return INDEX_ERR_NOMEM;
        capacity *= 2;
    }
    bigger = realloc(b->data, capacity);
    if (bigger == NULL)
        return INDEX_ERR_NOMEM;
    b->data = bigger;
    b->capacity = capacity;
    return INDEX_OK;
}

/*
 * Function    : buf_put_varint / buf_put_bytes
 * Description : Append to a buffer; the caller has reserved the space
 *               (VARINT_MAX_BYTES per varint)
 */
static void buf_put_varint(ByteBuf *b, uint64_t value)
{
    while (value > VARINT_DATA_MASK)
    {
        b->data[b->len++] = (unsigned char)((value & VARINT_DATA_MASK) | VARINT_MORE_FLAG);
        value >>= 7;
    }
    b->data[b->len++] = (unsigned char)value;
}

static void buf_put_bytes(ByteBuf *b, const void *data, size_t size)
{
    memcpy(b->data + b->len, data, size);
    b->len += size;
}

/*
 * Function    : collect_node / compare_words / compare_runs
 * Description : Helpers to write words alphabetically and runs by document
 */
static void collect_node(HashNode *node, void *ctx)
{
    NodeList *list = ctx;

    if (list->count < list->capacity)
        list->nodes[list->count++] = node;
}

static int compare_words(const void *a, const void *b)
{
    const HashNode *x = *(const HashNode *const *)a;
    const HashNode *y = *(const HashNode *const *)b;

    return strcmp(x->word, y->word);
}

static int compare_runs(const void *a, const void *b)
{
    const Run *x = a;
    const Run *y = b;

    return (x->doc > y->doc) - (x->doc < y->doc);
}

/*
 * Function    : encode_word
 * Description : Encodes one word with its occurrences grouped per document.
 *               'runs' must have room for idx->doc_count entries.
 * Return      : INDEX_OK, INDEX_ERR_NOMEM, or INDEX_ERR_INTERNAL if the list
 *               breaks the invariant described at the top of this file
 */
static int encode_word(ByteBuf *b, const InvertedIndex *idx, const HashNode *node, Run *runs)
{
    const FilePosition *fp = node->positions;
    size_t run_count = 0, word_len, r, k;

    /* Split the list into per-document runs */
    while (fp != NULL)
    {
        const char *name = fp->filename;
        long previous = 0;
        Run *run;

        if (run_count == idx->doc_count)
            return INDEX_ERR_INTERNAL;
        run = &runs[run_count++];
        if (doc_id_of(idx, name, &run->doc) != 0)
            return INDEX_ERR_INTERNAL;
        run->first = fp;
        run->count = 0;

        for (; fp != NULL && fp->filename == name; fp = fp->next)
        {
            if (fp->position < previous)
                return INDEX_ERR_INTERNAL;
            previous = fp->position;
            run->count++;
        }
    }

    /* At most one run per document, written in document order */
    qsort(runs, run_count, sizeof *runs, compare_runs);
    for (r = 1; r < run_count; r++)
    {
        if (runs[r].doc == runs[r - 1].doc)
            return INDEX_ERR_INTERNAL;
    }

    word_len = strlen(node->word);
    if (buf_reserve(b, 2 * VARINT_MAX_BYTES + word_len) != INDEX_OK)
        return INDEX_ERR_NOMEM;
    buf_put_varint(b, word_len);
    buf_put_bytes(b, node->word, word_len);
    buf_put_varint(b, run_count);

    for (r = 0; r < run_count; r++)
    {
        long previous = 0;

        if (buf_reserve(b, (runs[r].count + 2) * VARINT_MAX_BYTES) != INDEX_OK)
            return INDEX_ERR_NOMEM;
        buf_put_varint(b, runs[r].doc);
        buf_put_varint(b, runs[r].count);
        for (k = 0, fp = runs[r].first; k < runs[r].count; k++, fp = fp->next)
        {
            buf_put_varint(b, (uint64_t)(fp->position - previous));
            previous = fp->position;
        }
    }
    return INDEX_OK;
}

/*
 * Function    : encode_header
 * Description : Encodes the file header and the document table
 */
static int encode_header(ByteBuf *b, const InvertedIndex *idx, size_t word_count)
{
    size_t i;

    if (buf_reserve(b, INDEX_MAGIC_LEN + 3 * VARINT_MAX_BYTES) != INDEX_OK)
        return INDEX_ERR_NOMEM;
    buf_put_bytes(b, INDEX_MAGIC, INDEX_MAGIC_LEN);
    buf_put_varint(b, INDEX_VERSION);
    buf_put_varint(b, idx->doc_count);
    buf_put_varint(b, word_count);

    for (i = 0; i < idx->doc_count; i++)
    {
        size_t len = strlen(idx->docs[i]);

        if (buf_reserve(b, VARINT_MAX_BYTES + len) != INDEX_OK)
            return INDEX_ERR_NOMEM;
        buf_put_varint(b, len);
        buf_put_bytes(b, idx->docs[i], len);
    }
    return INDEX_OK;
}

/*
 * Function    : save_worker
 * Description : Encoding thread. Takes chunks of words from the shared
 *               counter until none are left or an error occurred.
 */
static void *save_worker(void *arg)
{
    SaveJob *job = arg;
    size_t doc_count = job->idx->doc_count;
    Run *runs = malloc((doc_count > 0 ? doc_count : 1) * sizeof *runs);
    int no_error = INDEX_OK;

    if (runs == NULL)
    {
        __atomic_compare_exchange_n(&job->error, &no_error, INDEX_ERR_NOMEM, 0,
                                    __ATOMIC_RELAXED, __ATOMIC_RELAXED);
        return NULL;
    }

    for (;;)
    {
        size_t chunk = __atomic_fetch_add(&job->next_chunk, 1, __ATOMIC_RELAXED);
        size_t first, last, i;

        if (chunk >= job->chunk_count || __atomic_load_n(&job->error, __ATOMIC_RELAXED) != INDEX_OK)
            break;

        first = chunk * job->word_count / job->chunk_count;
        last = (chunk + 1) * job->word_count / job->chunk_count;
        for (i = first; i < last; i++)
        {
            int rc = encode_word(&job->chunks[chunk], job->idx, job->words[i], runs);

            if (rc != INDEX_OK)
            {
                no_error = INDEX_OK;
                __atomic_compare_exchange_n(&job->error, &no_error, rc, 0,
                                            __ATOMIC_RELAXED, __ATOMIC_RELAXED);
                break;
            }
        }
    }
    free(runs);
    return NULL;
}

/*
 * Function    : encode_words
 * Description : Encodes every word using 'threads' threads (the calling
 *               thread included). Works with fewer threads if some cannot
 *               be started.
 */
static int encode_words(SaveJob *job, int threads)
{
    pthread_t *workers = NULL;
    int created = 0, i;

    if (threads > 1)
        workers = malloc((size_t)(threads - 1) * sizeof *workers);
    if (workers != NULL)
    {
        for (; created < threads - 1; created++)
        {
            if (pthread_create(&workers[created], NULL, save_worker, job) != 0)
                break;
        }
    }
    save_worker(job);
    for (i = 0; i < created; i++)
        pthread_join(workers[i], NULL);
    free(workers);
    return __atomic_load_n(&job->error, __ATOMIC_RELAXED);
}

/*
 * Function    : write_buffer
 * Description : fwrite() wrapper for a whole buffer
 * Return      : 1 on success, 0 on error
 */
static int write_buffer(FILE *fp, const void *data, size_t size)
{
    return size == 0 || fwrite(data, 1, size, fp) == size;
}

/*
 * Function    : index_save
 * Description : Writes the index to 'path', encoding with 'threads' threads.
 *               The data goes to 'path'.tmp first and is renamed over 'path'
 *               only once complete, so a failure never destroys an existing
 *               index.
 * Return      : INDEX_OK, INDEX_ERR_OPEN / INDEX_ERR_WRITE (errno is set),
 *               INDEX_ERR_NOMEM or INDEX_ERR_INTERNAL
 */
int index_save(InvertedIndex *idx, const char *path, int threads)
{
    NodeList words = { NULL, 0, 0 };
    ByteBuf header = { NULL, 0, 0 };
    SaveJob job;
    char *tmp_path = NULL;
    FILE *fp;
    int rc = INDEX_OK;
    int saved_errno = 0;
    size_t i;

    memset(&job, 0, sizeof job);
    if (threads < 1)
        threads = 1;

    words.capacity = ht_size(idx->table);
    words.nodes = malloc((words.capacity > 0 ? words.capacity : 1) * sizeof *words.nodes);
    tmp_path = malloc(strlen(path) + sizeof INDEX_TMP_SUFFIX);
    if (words.nodes == NULL || tmp_path == NULL)
    {
        rc = INDEX_ERR_NOMEM;
        goto done;
    }
    strcpy(tmp_path, path);
    strcat(tmp_path, INDEX_TMP_SUFFIX);

    ht_foreach(idx->table, collect_node, &words);
    qsort(words.nodes, words.count, sizeof *words.nodes, compare_words);

    /* Encode everything in memory: header first, then the words in chunks */
    rc = encode_header(&header, idx, words.count);
    if (rc != INDEX_OK)
        goto done;

    job.idx = idx;
    job.words = words.nodes;
    job.word_count = words.count;
    job.chunk_count = (size_t)threads * SAVE_CHUNKS_PER_THREAD;
    if (job.chunk_count > words.count)
        job.chunk_count = words.count;
    job.chunks = calloc(job.chunk_count > 0 ? job.chunk_count : 1, sizeof *job.chunks);
    if (job.chunks == NULL)
    {
        rc = INDEX_ERR_NOMEM;
        goto done;
    }
    job.error = INDEX_OK;
    rc = encode_words(&job, threads);
    if (rc != INDEX_OK)
        goto done;

    /* Write the pieces in order, then replace the old index */
    fp = fopen(tmp_path, "wb");
    if (fp == NULL)
    {
        rc = INDEX_ERR_OPEN;
        saved_errno = errno;
        goto done;
    }
    if (!write_buffer(fp, header.data, header.len))
        rc = INDEX_ERR_WRITE;
    for (i = 0; i < job.chunk_count && rc == INDEX_OK; i++)
    {
        if (!write_buffer(fp, job.chunks[i].data, job.chunks[i].len))
            rc = INDEX_ERR_WRITE;
    }
    if (rc == INDEX_OK && !write_buffer(fp, INDEX_END_MAGIC, INDEX_MAGIC_LEN))
        rc = INDEX_ERR_WRITE;
    if (rc != INDEX_OK)
        saved_errno = errno;
    if (fclose(fp) != 0 && rc == INDEX_OK)
    {
        rc = INDEX_ERR_WRITE;
        saved_errno = errno;
    }
    if (rc == INDEX_OK && rename(tmp_path, path) != 0)
    {
        rc = INDEX_ERR_WRITE;
        saved_errno = errno;
    }
    if (rc != INDEX_OK)
        remove(tmp_path);

done:
    if (job.chunks != NULL)
    {
        for (i = 0; i < job.chunk_count; i++)
            free(job.chunks[i].data);
        free(job.chunks);
    }
    free(header.data);
    free(words.nodes);
    free(tmp_path);
    errno = saved_errno;
    return rc;
}

/*
 * Function    : read_varint / read_bytes / remaining
 * Description : Bounds-checked input helpers; errors set r->bad
 */
static uint64_t read_varint(Reader *r)
{
    uint64_t value = 0;
    unsigned shift;

    for (shift = 0; !r->bad && r->pos < r->end && shift < 64; shift += 7)
    {
        unsigned char byte = *r->pos++;

        value |= (uint64_t)(byte & VARINT_DATA_MASK) << shift;
        if ((byte & VARINT_MORE_FLAG) == 0)
            return value;
    }
    r->bad = 1;
    return 0;
}

static const unsigned char *read_bytes(Reader *r, uint64_t size)
{
    const unsigned char *start = r->pos;

    if (r->bad || size > (uint64_t)(r->end - r->pos))
    {
        r->bad = 1;
        return NULL;
    }
    r->pos += size;
    return start;
}

static uint64_t remaining(const Reader *r)
{
    return (uint64_t)(r->end - r->pos);
}

/*
 * Function    : read_whole_file
 * Description : Reads a file into a malloc'ed buffer
 */
static int read_whole_file(const char *path, unsigned char **data, size_t *size)
{
    FILE *fp = fopen(path, "rb");
    unsigned char *buf = NULL;
    size_t capacity = 0, len = 0, n;
    int rc = INDEX_OK;
    int saved_errno;

    if (fp == NULL)
        return INDEX_ERR_OPEN;

    for (;;)
    {
        if (len == capacity)
        {
            size_t new_capacity = capacity > 0 ? capacity * 2 : INDEX_IO_BUFFER;
            unsigned char *bigger = new_capacity > capacity ? realloc(buf, new_capacity) : NULL;

            if (bigger == NULL)
            {
                rc = INDEX_ERR_NOMEM;
                break;
            }
            buf = bigger;
            capacity = new_capacity;
        }
        n = fread(buf + len, 1, capacity - len, fp);
        len += n;
        if (n == 0)
            break;
    }
    if (rc == INDEX_OK && ferror(fp))
        rc = INDEX_ERR_READ;

    saved_errno = errno;
    fclose(fp);
    errno = saved_errno;

    if (rc != INDEX_OK)
    {
        free(buf);
        return rc;
    }
    *data = buf;
    *size = len;
    return INDEX_OK;
}

/*
 * Function    : load_word
 * Description : Decodes one word record and adds it to idx->table. The
 *               occurrences are taken from idx->blocks, so nothing needs to
 *               be released here on error: index_free() frees the blocks.
 */
static int load_word(Reader *r, InvertedIndex *idx)
{
    char word[INDEX_MAX_WORD_LEN + 1];
    FilePosition *head = NULL, *tail = NULL;
    const unsigned char *bytes;
    uint64_t len, run_count, run, k;

    len = read_varint(r);
    bytes = read_bytes(r, len);
    if (bytes == NULL || len == 0 || len > INDEX_MAX_WORD_LEN || memchr(bytes, '\0', len) != NULL)
        return INDEX_ERR_FORMAT;
    memcpy(word, bytes, len);
    word[len] = '\0';

    run_count = read_varint(r);
    if (r->bad || run_count == 0 || run_count > idx->doc_count)
        return INDEX_ERR_FORMAT;

    for (run = 0; run < run_count; run++)
    {
        uint64_t doc = read_varint(r);
        uint64_t count = read_varint(r);
        uint64_t position = 0;

        /* Every occurrence takes at least one byte, which bounds 'count' */
        if (r->bad || doc >= idx->doc_count || count == 0 || count > remaining(r))
            return INDEX_ERR_FORMAT;

        for (k = 0; k < count; k++)
        {
            uint64_t delta = read_varint(r);
            FilePosition *occurrence;

            if (r->bad || delta > (uint64_t)LONG_MAX - position)
                return INDEX_ERR_FORMAT;
            position += delta;

            occurrence = new_position(&idx->blocks);
            if (occurrence == NULL)
                return INDEX_ERR_NOMEM;
            occurrence->filename = idx->docs[doc];
            occurrence->position = (long)position;
            occurrence->next = NULL;
            if (tail != NULL)
                tail->next = occurrence;
            else
                head = occurrence;
            tail = occurrence;
        }
    }

    if (ht_insert_list(idx->table, word, head, tail) != 0)
        return INDEX_ERR_NOMEM;
    return INDEX_OK;
}

/*
 * Function    : index_load
 * Description : Reads an index written by index_save() and validates it
 * Return      : INDEX_OK and *out, INDEX_ERR_OPEN / INDEX_ERR_READ (errno is
 *               set), INDEX_ERR_FORMAT or INDEX_ERR_NOMEM
 */
int index_load(const char *path, InvertedIndex **out)
{
    unsigned char *data = NULL;
    size_t size = 0;
    InvertedIndex *idx = NULL;
    NameRef *refs = NULL;
    const unsigned char *magic;
    uint64_t version, doc_count, word_count, i;
    Reader r;
    int rc;

    *out = NULL;
    rc = read_whole_file(path, &data, &size);
    if (rc != INDEX_OK)
        return rc;

    r.pos = data;
    r.end = data + size;
    r.bad = 0;

    magic = read_bytes(&r, INDEX_MAGIC_LEN);
    version = read_varint(&r);
    doc_count = read_varint(&r);
    word_count = read_varint(&r);
    if (magic == NULL || memcmp(magic, INDEX_MAGIC, INDEX_MAGIC_LEN) != 0 || r.bad ||
        version != INDEX_VERSION || doc_count > remaining(&r) || word_count > remaining(&r))
    {
        rc = INDEX_ERR_FORMAT;
        goto done;
    }

    idx = index_alloc();
    refs = malloc((doc_count > 0 ? doc_count : 1) * sizeof *refs);
    if (idx == NULL || refs == NULL)
    {
        rc = INDEX_ERR_NOMEM;
        goto done;
    }

    for (i = 0; i < doc_count; i++)
    {
        uint64_t len = read_varint(&r);
        const unsigned char *name = read_bytes(&r, len);

        if (name == NULL || len == 0 || memchr(name, '\0', len) != NULL)
        {
            rc = INDEX_ERR_FORMAT;
            goto done;
        }
        refs[i].name = (const char *)name;
        refs[i].len = len;
    }
    rc = set_documents(idx, refs, doc_count);
    if (rc != INDEX_OK)
        goto done;

    /* Size the table for the known word count so it never has to grow */
    idx->table = ht_create(word_count + word_count / 3, 0);
    if (idx->table == NULL)
    {
        rc = INDEX_ERR_NOMEM;
        goto done;
    }

    for (i = 0; i < word_count && rc == INDEX_OK; i++)
        rc = load_word(&r, idx);

    if (rc == INDEX_OK)
    {
        magic = read_bytes(&r, INDEX_MAGIC_LEN);
        if (magic == NULL || memcmp(magic, INDEX_END_MAGIC, INDEX_MAGIC_LEN) != 0 || r.pos != r.end)
            rc = INDEX_ERR_FORMAT;
    }

done:
    free(refs);
    free(data);
    if (rc != INDEX_OK)
    {
        index_free(idx);
        return rc;
    }
    *out = idx;
    return INDEX_OK;
}

/*
 * Function    : index_error_text
 * Description : Human readable text for an INDEX_* return code
 */
const char *index_error_text(int code)
{
    switch (code)
    {
        case INDEX_OK:           return "success";
        case INDEX_ERR_OPEN:     return "could not open file";
        case INDEX_ERR_READ:     return "read error";
        case INDEX_ERR_WRITE:    return "write error";
        case INDEX_ERR_NOMEM:    return "out of memory";
        case INDEX_ERR_FORMAT:   return "file is corrupted or is not a valid index";
        case INDEX_ERR_INTERNAL: return "index failed an internal consistency check";
        default:                 return "unknown error";
    }
}
