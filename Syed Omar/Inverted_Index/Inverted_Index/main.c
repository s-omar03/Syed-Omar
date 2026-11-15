/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : main.c
 *           Descriptions   : Main controller: command line interface, worker thread pool,
 *                            index persistence and timing
 *           Objective      : Index many text files in parallel and search the saved index
 *           Usage          : ./search index [-t <threads>] <file1> <file2> ...
 *                            ./search search [-v] <word>
 *                            ./search delete
 *           Output         : Progress and timing for "index", "<file> at position <n>" lines
 *                            for "search", a confirmation for "delete"
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* clock_gettime(), strerror_r() */

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hash_table.h"
#include "index.h"
#include "search.h"

#define INDEX_FILE         "index.dat"   /* Persistent store                    */
#define DEFAULT_THREADS    8             /* Worker threads when -t is not given */
#define MAX_THREADS        256           /* Largest value accepted for -t       */
#define ERROR_TEXT_SIZE    128           /* Buffer for strerror_r()             */
#define NANOS_PER_SECOND   1e9

/* Shared state of the thread pool: documents are handed out one at a time */
typedef struct
{
    InvertedIndex *index;
    pthread_mutex_t lock;        /* Protects every field below           */
    size_t next_doc;             /* Next document id to hand out         */
    size_t indexed;              /* Documents indexed successfully       */
    size_t failed;               /* Documents that could not be read     */
    int out_of_memory;           /* Set when a worker ran out of memory  */
} WorkQueue;

/*
 * Function    : print_usage
 * Description : Prints the command summary
 */
static void print_usage(FILE *out, const char *prog)
{
    fprintf(out,
            "Usage:\n"
            "  %s index <file1> <file2> ...  # Create index for given files\n"
            "  %s search <word>              # Search for a word\n"
            "  %s delete                     # Delete index file\n"
            "Options:\n"
            "  index -t <threads>   Number of worker threads (1-%d, default %d)\n"
            "  search -v            Also print match counts and timings\n",
            prog, prog, prog, MAX_THREADS, DEFAULT_THREADS);
}

/*
 * Function    : now_seconds
 * Description : Monotonic wall clock time, for benchmarking
 */
static double now_seconds(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / NANOS_PER_SECOND;
}

/*
 * Function    : errno_text
 * Description : Thread-safe strerror()
 */
static const char *errno_text(int err, char *buf, size_t size)
{
    if (strerror_r(err, buf, size) != 0)
        snprintf(buf, size, "error %d", err);
    return buf;
}

/*
 * Function    : parse_threads
 * Description : Validates the -t argument
 * Return      : 0 and *threads on success, -1 if the value is not valid
 */
static int parse_threads(const char *text, int *threads)
{
    char *end;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 1 || value > MAX_THREADS)
        return -1;
    *threads = (int)value;
    return 0;
}

/*
 * Function    : index_worker
 * Description : Thread pool worker. Repeatedly takes the next document from
 *               the mutex-protected counter and indexes it, until no
 *               documents are left.
 */
static void *index_worker(void *arg)
{
    WorkQueue *queue = arg;
    char err[ERROR_TEXT_SIZE];

    for (;;)
    {
        const char *name;
        size_t doc;
        int rc, saved_errno;

        pthread_mutex_lock(&queue->lock);
        if (queue->next_doc >= queue->index->doc_count || queue->out_of_memory)
        {
            pthread_mutex_unlock(&queue->lock);
            break;
        }
        doc = queue->next_doc++;
        pthread_mutex_unlock(&queue->lock);

        name = queue->index->docs[doc];
        printf("Thread %lu indexing %s\n", (unsigned long)pthread_self(), name);
        fflush(stdout);

        rc = index_add_document(queue->index, doc);
        saved_errno = errno;

        if (rc == INDEX_ERR_OPEN)
            fprintf(stderr, "Warning: Could not open file %s (%s)\n",
                    name, errno_text(saved_errno, err, sizeof err));
        else if (rc == INDEX_ERR_READ)
            fprintf(stderr, "Warning: Could not read file %s (%s)\n",
                    name, errno_text(saved_errno, err, sizeof err));
        else if (rc == INDEX_ERR_NOMEM)
            fprintf(stderr, "Error: Out of memory while indexing %s\n", name);

        pthread_mutex_lock(&queue->lock);
        if (rc == INDEX_OK)
            queue->indexed++;
        else if (rc == INDEX_ERR_NOMEM)
            queue->out_of_memory = 1;
        else
            queue->failed++;
        pthread_mutex_unlock(&queue->lock);
    }
    return NULL;
}

/*
 * Function    : run_thread_pool
 * Description : Starts 'threads' workers on the queue and waits for them
 * Return      : Number of threads that actually ran (at least 1)
 */
static int run_thread_pool(WorkQueue *queue, int threads)
{
    pthread_t *workers = malloc((size_t)threads * sizeof *workers);
    int created = 0, i;

    if (workers != NULL)
    {
        for (; created < threads; created++)
        {
            if (pthread_create(&workers[created], NULL, index_worker, queue) != 0)
                break;
        }
    }
    if (created < threads)
        fprintf(stderr, "Warning: Only %d of %d threads could be started.\n", created, threads);

    if (created == 0)
    {
        index_worker(queue);            /* Fall back to the main thread */
        free(workers);
        return 1;
    }
    for (i = 0; i < created; i++)
        pthread_join(workers[i], NULL);
    free(workers);
    return created;
}

/*
 * Function    : cmd_index
 * Description : "index" command: builds the index of the given files with
 *               the thread pool and saves it to INDEX_FILE
 */
static int cmd_index(const char *prog, int argc, char **argv)
{
    int threads = DEFAULT_THREADS;
    int arg = 0, rc, save_errno;
    size_t requested;
    InvertedIndex *idx;
    WorkQueue queue;
    double start, elapsed;
    char err[ERROR_TEXT_SIZE];

    /* Options come before the file names; "--" ends the options */
    while (arg < argc && argv[arg][0] == '-')
    {
        if (strcmp(argv[arg], "--") == 0)
        {
            arg++;
            break;
        }
        if (strcmp(argv[arg], "-t") != 0)
        {
            fprintf(stderr, "Error: Unknown option '%s'.\n", argv[arg]);
            print_usage(stderr, prog);
            return EXIT_FAILURE;
        }
        if (arg + 1 >= argc || parse_threads(argv[arg + 1], &threads) != 0)
        {
            fprintf(stderr, "Error: -t needs a thread count between 1 and %d.\n", MAX_THREADS);
            return EXIT_FAILURE;
        }
        arg += 2;
    }
    if (arg >= argc)
    {
        fprintf(stderr, "Error: No files given to index.\n");
        print_usage(stderr, prog);
        return EXIT_FAILURE;
    }

    requested = (size_t)(argc - arg);
    idx = index_create(argv + arg, requested);
    if (idx == NULL)
    {
        fprintf(stderr, "Error: Out of memory.\n");
        return EXIT_FAILURE;
    }
    if (idx->doc_count < requested)
        printf("Note: %zu duplicate file name(s) ignored.\n", requested - idx->doc_count);

    /* More threads than files would only sit idle */
    if ((size_t)threads > idx->doc_count)
        threads = (int)idx->doc_count;

    printf("Indexing %zu files using %d threads...\n", idx->doc_count, threads);
    fflush(stdout);

    start = now_seconds();
    queue.index = idx;
    queue.next_doc = 0;
    queue.indexed = 0;
    queue.failed = 0;
    queue.out_of_memory = 0;
    if (pthread_mutex_init(&queue.lock, NULL) != 0)
    {
        fprintf(stderr, "Error: Could not initialise the work queue.\n");
        index_free(idx);
        return EXIT_FAILURE;
    }
    threads = run_thread_pool(&queue, threads);
    pthread_mutex_destroy(&queue.lock);

    if (queue.out_of_memory)
    {
        fprintf(stderr, "Error: Out of memory while indexing. Index not saved.\n");
        index_free(idx);
        return EXIT_FAILURE;
    }
    if (queue.indexed == 0)
    {
        fprintf(stderr, "Error: None of the files could be indexed. Index not saved.\n");
        index_free(idx);
        return EXIT_FAILURE;
    }

    rc = index_save(idx, INDEX_FILE, threads);
    save_errno = errno;
    elapsed = now_seconds() - start;
    index_free(idx);

    if (rc != INDEX_OK)
    {
        if (rc == INDEX_ERR_OPEN || rc == INDEX_ERR_WRITE)
            fprintf(stderr, "Error: Could not save index to %s (%s).\n",
                    INDEX_FILE, errno_text(save_errno, err, sizeof err));
        else
            fprintf(stderr, "Error: Could not save index to %s: %s.\n",
                    INDEX_FILE, index_error_text(rc));
        return EXIT_FAILURE;
    }

    printf("Indexing complete. Index saved to %s\n", INDEX_FILE);
    if (queue.failed > 0)
        printf("Note: %zu of %zu files could not be indexed (see warnings above).\n",
               queue.failed, queue.indexed + queue.failed);
    printf("Elapsed time: %.6f seconds (threads=%d, files=%zu)\n",
           elapsed, threads, queue.indexed + queue.failed);
    return EXIT_SUCCESS;
}

/*
 * Function    : cmd_search
 * Description : "search" command: loads INDEX_FILE and prints every
 *               occurrence of one word (case-insensitive)
 */
static int cmd_search(const char *prog, int argc, char **argv)
{
    char word[INDEX_MAX_WORD_LEN + 1];
    char err[ERROR_TEXT_SIZE];
    InvertedIndex *idx;
    const FilePosition *hits;
    SearchStats stats = { 0, 0 };
    double load_start, lookup_start, lookup_end;
    int verbose = 0, arg = 0, rc, load_errno;

    if (arg < argc && strcmp(argv[arg], "-v") == 0)
    {
        verbose = 1;
        arg++;
    }
    if (argc - arg != 1)
    {
        fprintf(stderr, "Error: Please give exactly one word to search for.\n");
        print_usage(stderr, prog);
        return EXIT_FAILURE;
    }

    rc = search_normalize(argv[arg], word, sizeof word);
    if (rc == SEARCH_ERR_EMPTY)
    {
        fprintf(stderr, "Error: '%s' does not contain a word to search for.\n", argv[arg]);
        return EXIT_FAILURE;
    }
    if (rc == SEARCH_ERR_MULTI)
    {
        fprintf(stderr, "Error: '%s' contains more than one word. Please search for a single word.\n",
                argv[arg]);
        return EXIT_FAILURE;
    }

    load_start = now_seconds();
    rc = index_load(INDEX_FILE, &idx);
    load_errno = errno;
    if (rc == INDEX_ERR_OPEN && load_errno == ENOENT)
    {
        fprintf(stderr, "Error: No index exists. Please index files first.\n");
        return EXIT_FAILURE;
    }
    if (rc == INDEX_ERR_OPEN || rc == INDEX_ERR_READ)
    {
        fprintf(stderr, "Error: Could not read index file '%s' (%s).\n",
                INDEX_FILE, errno_text(load_errno, err, sizeof err));
        return EXIT_FAILURE;
    }
    if (rc != INDEX_OK)
    {
        fprintf(stderr, "Error: Could not load index file '%s': %s.\n", INDEX_FILE, index_error_text(rc));
        return EXIT_FAILURE;
    }

    lookup_start = now_seconds();
    hits = search_lookup(idx, word);
    lookup_end = now_seconds();

    if (hits == NULL)
        printf("No matches found for '%s'.\n", word);
    else
        stats = search_print_results(hits, stdout);

    if (verbose)
    {
        printf("\nFound %zu occurrence(s) in %zu file(s).\n", stats.occurrences, stats.documents);
        printf("Index load time: %.6f seconds (%zu words, %zu files)\n",
               lookup_start - load_start, ht_size(idx->table), idx->doc_count);
        printf("Search time:     %.6f seconds\n", lookup_end - lookup_start);
    }
    index_free(idx);
    return EXIT_SUCCESS;
}

/*
 * Function    : cmd_delete
 * Description : "delete" command: removes INDEX_FILE
 */
static int cmd_delete(const char *prog, int argc)
{
    char err[ERROR_TEXT_SIZE];

    if (argc != 0)
    {
        fprintf(stderr, "Error: delete does not take any arguments.\n");
        print_usage(stderr, prog);
        return EXIT_FAILURE;
    }
    if (remove(INDEX_FILE) == 0)
    {
        printf("Index file '%s' deleted successfully.\n", INDEX_FILE);
        return EXIT_SUCCESS;
    }
    if (errno == ENOENT)
        fprintf(stderr, "Error: No index exists. Nothing to delete.\n");
    else
        fprintf(stderr, "Error: Could not delete index file '%s' (%s).\n",
                INDEX_FILE, errno_text(errno, err, sizeof err));
    return EXIT_FAILURE;
}

/*
 * Function    : main
 * Description : Dispatches to the requested command
 */
int main(int argc, char *argv[])
{
    const char *prog = (argc > 0 && argv[0] != NULL) ? argv[0] : "search";

    if (argc < 2)
    {
        print_usage(stderr, prog);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[1], "index") == 0)
        return cmd_index(prog, argc - 2, argv + 2);
    if (strcmp(argv[1], "search") == 0)
        return cmd_search(prog, argc - 2, argv + 2);
    if (strcmp(argv[1], "delete") == 0)
        return cmd_delete(prog, argc - 2);
    if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
    {
        print_usage(stdout, prog);
        return EXIT_SUCCESS;
    }

    fprintf(stderr, "Invalid command.\n");
    print_usage(stderr, prog);
    return EXIT_FAILURE;
}
