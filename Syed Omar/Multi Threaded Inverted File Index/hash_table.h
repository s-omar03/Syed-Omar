/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : hash_table.h
 *           Descriptions   : Data structures and API of the thread-safe word hash table
 *           Objective      : Map every indexed word to the list of places where it occurs
 *           Usage          : #include "hash_table.h"
 *           Output         : None (library module)
 *
###############################################################################################*/

#ifndef HASH_TABLE_H
#define HASH_TABLE_H

#include <stddef.h>
#include <pthread.h>

#define HT_LOCK_STRIPES     64  /* Mutexes guarding the buckets (power of two) */
#define HT_MAX_LOAD_NUM     3   /* Grow when size > capacity * NUM / DEN       */
#define HT_MAX_LOAD_DEN     4   /*   i.e. keep the load factor under 0.75      */
#define HT_GROWTH_FACTOR    2   /* Bucket array multiplier (power of two)      */

/* One occurrence of a word inside a document */
typedef struct FilePosition
{
    const char *filename;          /* Source document name (shared, not owned) */
    long position;                 /* Character offset in file                 */
    struct FilePosition *next;     /* Next occurrence                          */
} FilePosition;

/* One unique word together with all of its occurrences */
typedef struct HashNode
{
    char *word;                    /* The indexed word (owned by the node)     */
    FilePosition *positions;       /* Linked list of occurrences               */
    struct HashNode *next;         /* Collision resolution (separate chaining) */
} HashNode;

/* Hash table with separate chaining and lock striping */
typedef struct HashTable
{
    HashNode **buckets;                     /* Array of chains                  */
    size_t capacity;                        /* Bucket count, a power of two     */
    size_t size;                            /* Number of unique words           */
    pthread_mutex_t locks[HT_LOCK_STRIPES]; /* Stripe locks, see hash_table.c   */
    int owns_positions;                     /* 1: free occurrences on destroy   */
} HashTable;

/* Callback used by ht_foreach() */
typedef void (*ht_visit_fn)(HashNode *node, void *ctx);

HashTable *ht_create(size_t initial_capacity, int owns_positions);
void ht_destroy(HashTable *ht);

int ht_insert(HashTable *ht, const char *word, const char *filename, long position);
int ht_insert_list(HashTable *ht, const char *word, FilePosition *head, FilePosition *tail);
void ht_merge(HashTable *dst, HashTable *src);

const FilePosition *ht_lookup(HashTable *ht, const char *word);
size_t ht_size(HashTable *ht);
void ht_foreach(HashTable *ht, ht_visit_fn visit, void *ctx);

void ht_free_positions(FilePosition *list);

#endif
