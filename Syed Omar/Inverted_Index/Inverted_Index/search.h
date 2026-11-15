/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : search.h
 *           Descriptions   : Search engine API (query normalization, lookup, result printing)
 *           Objective      : Case-insensitive word search on a loaded inverted index
 *           Usage          : #include "search.h"
 *           Output         : None (library module)
 *
###############################################################################################*/

#ifndef SEARCH_H
#define SEARCH_H

#include <stdio.h>
#include "index.h"

/* Return codes of search_normalize() */
#define SEARCH_OK            0
#define SEARCH_ERR_EMPTY    -1   /* Query has no letters or digits     */
#define SEARCH_ERR_MULTI    -2   /* Query contains more than one word  */

/* Summary of a printed result list */
typedef struct
{
    size_t occurrences;          /* Number of lines printed            */
    size_t documents;            /* Number of distinct files           */
} SearchStats;

int search_normalize(const char *term, char *out, size_t out_size);
const FilePosition *search_lookup(InvertedIndex *idx, const char *word);
SearchStats search_print_results(const FilePosition *hits, FILE *out);

#endif
