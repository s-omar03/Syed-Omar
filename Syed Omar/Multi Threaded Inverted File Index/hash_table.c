/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : hash_table.c
 *           Descriptions   : Thread-safe hash table mapping a word to its list of occurrences
 *           Objective      : O(1) average insert and lookup for the inverted index, usable by
 *                            many indexing threads at the same time
 *           Usage          : Used by index.c and search.c through hash_table.h
 *           Output         : None (library module)
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* strdup() */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "hash_table.h"

#define FNV_OFFSET_BASIS 14695981039346656037ULL   /* FNV-1a 64-bit parameters */
#define FNV_PRIME        1099511628211ULL

/*
 * Locking scheme (lock striping)
 * ------------------------------
 * The table owns HT_LOCK_STRIPES mutexes. A word is always protected by the
 * stripe selected by the low bits of its hash. The bucket count is a power of
 * two and never smaller than the stripe count, so all words of one bucket map
 * to the same stripe whatever the current capacity is. Therefore:
 *   - insert, merge and lookup take exactly one stripe lock;
 *   - growing and iterating take every stripe lock, in ascending order.
 * Threads working on different stripes never wait for each other, and no
 * thread ever holds one stripe lock while waiting for another, so the
 * scheme cannot deadlock.
 *
 * 'size' and 'capacity' are also read outside the locks (to decide whether
 * to grow), so they are accessed with GCC __atomic builtins.
 */

/*
 * Function    : hash_word
 * Description : FNV-1a hash of a NUL-terminated word
 */
static uint64_t hash_word(const char *word)
{
    uint64_t hash = FNV_OFFSET_BASIS;

    while (*word != '\0')
    {
        hash ^= (unsigned char)*word++;
        hash *= FNV_PRIME;
    }
    return hash;
}

/*
 * Function    : stripe_lock
 * Description : Returns the mutex that protects every word with this hash
 */
static pthread_mutex_t *stripe_lock(HashTable *ht, uint64_t hash)
{
    return &ht->locks[hash & (HT_LOCK_STRIPES - 1)];
}

/*
 * Function    : round_up_capacity
 * Description : Smallest power of two >= n, and never below the stripe count
 */
static size_t round_up_capacity(size_t n)
{
    size_t capacity = HT_LOCK_STRIPES;

    while (capacity < n && capacity <= SIZE_MAX / 2)
        capacity <<= 1;
    return capacity;
}

/*
 * Function    : over_loaded
 * Description : True when the load factor limit has been exceeded
 */
static int over_loaded(size_t size, size_t capacity)
{
    return size > capacity / HT_MAX_LOAD_DEN * HT_MAX_LOAD_NUM;
}

/*
 * Function    : find_node
 * Description : Walks one collision chain looking for 'word'
 */
static HashNode *find_node(HashNode *chain, const char *word)
{
    for (; chain != NULL; chain = chain->next)
    {
        if (strcmp(chain->word, word) == 0)
            return chain;
    }
    return NULL;
}

/*
 * Function    : new_node
 * Description : Allocates a node holding a private copy of 'word'
 */
static HashNode *new_node(const char *word)
{
    HashNode *node = malloc(sizeof *node);

    if (node == NULL)
        return NULL;
    node->word = strdup(word);
    if (node->word == NULL)
    {
        free(node);
        return NULL;
    }
    node->positions = NULL;
    node->next = NULL;
    return node;
}

/*
 * Function    : lock_all / unlock_all
 * Description : Take or release every stripe lock (always in the same order)
 */
static void lock_all(HashTable *ht)
{
    size_t i;

    for (i = 0; i < HT_LOCK_STRIPES; i++)
        pthread_mutex_lock(&ht->locks[i]);
}

static void unlock_all(HashTable *ht)
{
    size_t i = HT_LOCK_STRIPES;

    while (i-- > 0)
        pthread_mutex_unlock(&ht->locks[i]);
}

/*
 * Function    : grow
 * Description : Rehashes every node into a bucket array HT_GROWTH_FACTOR times
 *               larger. Runs with all stripes locked. If the allocation fails
 *               the table simply keeps its current size (longer chains).
 */
static void grow(HashTable *ht)
{
    size_t old_capacity, new_capacity, i;
    HashNode **buckets;

    lock_all(ht);
    old_capacity = ht->capacity;

    /* Another thread may have grown the table while we waited for the locks */
    if (over_loaded(__atomic_load_n(&ht->size, __ATOMIC_RELAXED), old_capacity) &&
        old_capacity <= SIZE_MAX / HT_GROWTH_FACTOR)
    {
        new_capacity = old_capacity * HT_GROWTH_FACTOR;
        buckets = calloc(new_capacity, sizeof *buckets);
        if (buckets != NULL)
        {
            for (i = 0; i < old_capacity; i++)
            {
                HashNode *node = ht->buckets[i];

                while (node != NULL)
                {
                    HashNode *next = node->next;
                    size_t index = hash_word(node->word) & (new_capacity - 1);

                    node->next = buckets[index];
                    buckets[index] = node;
                    node = next;
                }
            }
            free(ht->buckets);
            ht->buckets = buckets;
            __atomic_store_n(&ht->capacity, new_capacity, __ATOMIC_RELAXED);
        }
    }
    unlock_all(ht);
}

/*
 * Function    : grow_if_needed
 * Description : Cheap unlocked check, called after a new word was added
 */
static void grow_if_needed(HashTable *ht)
{
    if (over_loaded(__atomic_load_n(&ht->size, __ATOMIC_RELAXED),
                    __atomic_load_n(&ht->capacity, __ATOMIC_RELAXED)))
        grow(ht);
}

/*
 * Function    : ht_create
 * Description : Creates an empty table with at least 'initial_capacity'
 *               buckets. When 'owns_positions' is 1 the table frees the
 *               occurrence nodes on destroy (they must come from malloc,
 *               as with ht_insert); when 0 the caller manages their memory.
 * Return      : The table, or NULL when out of memory
 */
HashTable *ht_create(size_t initial_capacity, int owns_positions)
{
    HashTable *ht = calloc(1, sizeof *ht);
    size_t i;

    if (ht == NULL)
        return NULL;

    ht->owns_positions = owns_positions;
    ht->capacity = round_up_capacity(initial_capacity);
    ht->buckets = calloc(ht->capacity, sizeof *ht->buckets);
    if (ht->buckets == NULL)
    {
        free(ht);
        return NULL;
    }

    for (i = 0; i < HT_LOCK_STRIPES; i++)
    {
        if (pthread_mutex_init(&ht->locks[i], NULL) != 0)
        {
            while (i-- > 0)
                pthread_mutex_destroy(&ht->locks[i]);
            free(ht->buckets);
            free(ht);
            return NULL;
        }
    }
    return ht;
}

/*
 * Function    : ht_destroy
 * Description : Frees every node and word (and the occurrences when the
 *               table owns them), then the table itself
 */
void ht_destroy(HashTable *ht)
{
    size_t i;

    if (ht == NULL)
        return;

    for (i = 0; i < ht->capacity; i++)
    {
        HashNode *node = ht->buckets[i];

        while (node != NULL)
        {
            HashNode *next = node->next;

            if (ht->owns_positions)
                ht_free_positions(node->positions);
            free(node->word);
            free(node);
            node = next;
        }
    }
    for (i = 0; i < HT_LOCK_STRIPES; i++)
        pthread_mutex_destroy(&ht->locks[i]);
    free(ht->buckets);
    free(ht);
}

/*
 * Function    : ht_insert_list
 * Description : Puts the occurrence list head..tail in front of the list
 *               already stored for 'word' (creating the word if needed).
 *               The table takes ownership of the list on success.
 * Return      : 0 on success, -1 when out of memory
 */
int ht_insert_list(HashTable *ht, const char *word, FilePosition *head, FilePosition *tail)
{
    uint64_t hash = hash_word(word);
    pthread_mutex_t *lock = stripe_lock(ht, hash);
    HashNode **bucket;
    HashNode *node;
    int added = 0;

    pthread_mutex_lock(lock);
    bucket = &ht->buckets[hash & (ht->capacity - 1)];
    node = find_node(*bucket, word);
    if (node == NULL)
    {
        node = new_node(word);
        if (node == NULL)
        {
            pthread_mutex_unlock(lock);
            return -1;
        }
        node->next = *bucket;
        *bucket = node;
        __atomic_add_fetch(&ht->size, 1, __ATOMIC_RELAXED);
        added = 1;
    }
    tail->next = node->positions;
    node->positions = head;
    pthread_mutex_unlock(lock);

    if (added)
        grow_if_needed(ht);
    return 0;
}

/*
 * Function    : ht_insert
 * Description : Records one occurrence of 'word', allocated with malloc
 *               (so the table should own its positions). 'filename' is
 *               stored by pointer and must stay valid as long as the table.
 * Return      : 0 on success, -1 when out of memory
 */
int ht_insert(HashTable *ht, const char *word, const char *filename, long position)
{
    FilePosition *occurrence = malloc(sizeof *occurrence);

    if (occurrence == NULL)
        return -1;
    occurrence->filename = filename;
    occurrence->position = position;
    occurrence->next = NULL;

    if (ht_insert_list(ht, word, occurrence, occurrence) != 0)
    {
        free(occurrence);
        return -1;
    }
    return 0;
}

/*
 * Function    : absorb_node
 * Description : Moves one node taken out of another table into 'ht'.
 *               Reuses the node when the word is new, otherwise splices its
 *               occurrences into the existing node. Never allocates.
 */
static void absorb_node(HashTable *ht, HashNode *node)
{
    uint64_t hash = hash_word(node->word);
    pthread_mutex_t *lock = stripe_lock(ht, hash);
    FilePosition *tail = node->positions;
    HashNode **bucket;
    HashNode *existing;

    /* Find the end of the incoming list before taking the lock */
    while (tail != NULL && tail->next != NULL)
        tail = tail->next;

    pthread_mutex_lock(lock);
    bucket = &ht->buckets[hash & (ht->capacity - 1)];
    existing = find_node(*bucket, node->word);
    if (existing != NULL)
    {
        if (tail != NULL)
        {
            tail->next = existing->positions;
            existing->positions = node->positions;
        }
        pthread_mutex_unlock(lock);
        free(node->word);
        free(node);
        return;
    }
    node->next = *bucket;
    *bucket = node;
    __atomic_add_fetch(&ht->size, 1, __ATOMIC_RELAXED);
    pthread_mutex_unlock(lock);

    grow_if_needed(ht);
}

/*
 * Function    : ht_merge
 * Description : Moves every word of 'src' into 'dst'; 'src' is left empty.
 *               Each word's list is spliced in as one block, so occurrences
 *               coming from 'src' stay contiguous inside 'dst'.
 *               'dst' may be shared with other threads, 'src' must not be.
 *               Both tables should use the same owns_positions setting.
 */
void ht_merge(HashTable *dst, HashTable *src)
{
    size_t i;

    for (i = 0; i < src->capacity; i++)
    {
        HashNode *node = src->buckets[i];

        src->buckets[i] = NULL;
        while (node != NULL)
        {
            HashNode *next = node->next;

            absorb_node(dst, node);
            node = next;
        }
    }
    src->size = 0;
}

/*
 * Function    : ht_lookup
 * Description : Finds the occurrence list of 'word' (exact match)
 * Return      : Head of the list, or NULL when the word is not indexed
 */
const FilePosition *ht_lookup(HashTable *ht, const char *word)
{
    uint64_t hash = hash_word(word);
    pthread_mutex_t *lock = stripe_lock(ht, hash);
    const FilePosition *positions = NULL;
    HashNode *node;

    pthread_mutex_lock(lock);
    node = find_node(ht->buckets[hash & (ht->capacity - 1)], word);
    if (node != NULL)
        positions = node->positions;
    pthread_mutex_unlock(lock);
    return positions;
}

/*
 * Function    : ht_size
 * Description : Number of unique words in the table
 */
size_t ht_size(HashTable *ht)
{
    return __atomic_load_n(&ht->size, __ATOMIC_RELAXED);
}

/*
 * Function    : ht_foreach
 * Description : Calls 'visit' once for every word, with the whole table
 *               locked. 'visit' may change node->positions but must not
 *               call other ht_* functions on the same table.
 */
void ht_foreach(HashTable *ht, ht_visit_fn visit, void *ctx)
{
    size_t i;

    lock_all(ht);
    for (i = 0; i < ht->capacity; i++)
    {
        HashNode *node;

        for (node = ht->buckets[i]; node != NULL; node = node->next)
            visit(node, ctx);
    }
    unlock_all(ht);
}

/*
 * Function    : ht_free_positions
 * Description : Frees a whole occurrence list
 */
void ht_free_positions(FilePosition *list)
{
    while (list != NULL)
    {
        FilePosition *next = list->next;

        free(list);
        list = next;
    }
}
