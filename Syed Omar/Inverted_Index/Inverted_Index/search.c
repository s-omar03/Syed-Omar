/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : search.c
 *           Descriptions   : Search engine: normalizes the query, looks it up in the hash table
 *                            and formats the matches
 *           Objective      : Fast, case-insensitive word lookup with file names and positions
 *           Usage          : Used by the "search" command in main.c
 *           Output         : "<file> at position <offset>" for every occurrence
 *
###############################################################################################*/

#include <ctype.h>
#include "search.h"

/*
 * Function    : search_normalize
 * Description : Converts a query into the form words are stored in: lower
 *               case, without surrounding punctuation ("Success!" ->
 *               "success"). 'out' should hold INDEX_MAX_WORD_LEN + 1 bytes;
 *               longer words are truncated exactly like the indexer does.
 * Return      : SEARCH_OK, SEARCH_ERR_EMPTY or SEARCH_ERR_MULTI
 */
int search_normalize(const char *term, char *out, size_t out_size)
{
    const unsigned char *p = (const unsigned char *)term;
    size_t len = 0;

    if (out_size == 0)
        return SEARCH_ERR_EMPTY;

    while (*p != '\0' && !index_is_word_char(*p))
        p++;
    for (; *p != '\0' && index_is_word_char(*p); p++)
    {
        if (len < INDEX_MAX_WORD_LEN && len + 1 < out_size)
            out[len++] = (char)tolower(*p);
    }
    out[len] = '\0';
    while (*p != '\0' && !index_is_word_char(*p))
        p++;

    if (len == 0)
        return SEARCH_ERR_EMPTY;
    if (*p != '\0')
        return SEARCH_ERR_MULTI;
    return SEARCH_OK;
}

/*
 * Function    : search_lookup
 * Description : O(1) average hash table lookup of a normalized word
 * Return      : Occurrences ordered by file (input order) then position,
 *               or NULL when the word is not in the index
 */
const FilePosition *search_lookup(InvertedIndex *idx, const char *word)
{
    return ht_lookup(idx->table, word);
}

/*
 * Function    : search_print_results
 * Description : Prints one line per occurrence and counts what was printed
 */
SearchStats search_print_results(const FilePosition *hits, FILE *out)
{
    SearchStats stats = { 0, 0 };
    const char *last_file = NULL;

    for (; hits != NULL; hits = hits->next)
    {
        fprintf(out, "%s at position %ld\n", hits->filename, hits->position);
        stats.occurrences++;
        if (hits->filename != last_file)
        {
            stats.documents++;
            last_file = hits->filename;
        }
    }
    return stats;
}
