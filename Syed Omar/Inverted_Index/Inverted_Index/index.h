/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : index.h
 *           Descriptions   : Inverted index: document table, index builder and index.dat I/O
 *           Objective      : Build the word -> occurrences index from text files (one
 *                            document per thread call) and save/load it persistently
 *           Usage          : #include "index.h"
 *           Output         : None (library module)
 *
###############################################################################################*/

#ifndef INDEX_H
#define INDEX_H

#include <stddef.h>
#include "hash_table.h"

#define INDEX_MAX_WORD_LEN        256        /* Longer words are truncated to this */
#define INDEX_READ_CHUNK          65536      /* Bytes read from a document at once */
#define INDEX_IO_BUFFER           65536      /* Buffer size for index.dat I/O      */
#define INDEX_TABLE_CAPACITY      65536      /* Initial buckets, shared table      */
#define INDEX_DOC_TABLE_CAPACITY  4096       /* Initial buckets, per-document table*/
#define INDEX_MIN_BLOCK_POSITIONS 64         /* FilePositions in a first block     */
#define INDEX_MAX_BLOCK_POSITIONS 4096       /* Blocks double up to this size      */

/* Return codes of the index_* functions (errno holds the detail for I/O errors) */
#define INDEX_OK              0
#define INDEX_ERR_OPEN       -1   /* File could not be opened              */
#define INDEX_ERR_READ       -2   /* Error while reading a file            */
#define INDEX_ERR_WRITE      -3   /* Error while writing the index         */
#define INDEX_ERR_NOMEM      -4   /* Out of memory                         */
#define INDEX_ERR_FORMAT     -5   /* index.dat is corrupted or unsupported */
#define INDEX_ERR_INTERNAL   -6   /* Index failed a consistency check      */

/* Memory block that FilePosition nodes are carved from (see index.c) */
typedef struct PositionBlock
{
    struct PositionBlock *next;     /* Next block in the list      */
    size_t used;                    /* Entries handed out          */
    size_t capacity;                /* Entries in items[]          */
    FilePosition items[];           /* Flexible array member (C99) */
} PositionBlock;

/* The inverted index: indexed documents plus word -> occurrences table */
typedef struct InvertedIndex
{
    HashTable *table;        /* word -> list of FilePosition                 */
    char **docs;             /* Document names in input order (document ids) */
    size_t doc_count;        /* Number of documents                          */
    char *doc_names;         /* Single buffer holding all names of docs[]    */
    PositionBlock *blocks;   /* Memory of every FilePosition in 'table'      */
    pthread_mutex_t blocks_lock;  /* Protects 'blocks' while indexing        */
} InvertedIndex;

InvertedIndex *index_create(char *const files[], size_t count);
int index_add_document(InvertedIndex *idx, size_t doc_id);
int index_save(InvertedIndex *idx, const char *path, int threads);
int index_load(const char *path, InvertedIndex **out);
void index_free(InvertedIndex *idx);

int index_is_word_char(int c);
const char *index_error_text(int code);

#endif
