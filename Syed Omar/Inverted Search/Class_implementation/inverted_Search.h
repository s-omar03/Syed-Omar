#ifndef INVERTED_SEARCH_H
#define INVERTED_SEARCH_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>

//defining macros
#define FAILURE   -1
#define SUCCESS    0
#define FNAME_SIZE 30
#define WORD_SIZE  30
#define FILE_EMPTY -2
#define FILE_NOTAVAILABLE -3
#define REPEATATION -4
#define TABLE_SIZE 27   /* 26 letters (a-z) + 1 bucket for numbers/symbols */

//Structure for file list
typedef char data_t;
typedef struct file_node
{
    data_t file_name[FNAME_SIZE];
    struct file_node *link;

}Flist;

//Structure for link table
typedef struct linkTable_node
{
    int word_count;
    data_t file_name[FNAME_SIZE];
    struct linkTable_node *table_link;
}Ltable;

//structure to store word count
typedef struct word_node
{
    int file_count;
    data_t word[WORD_SIZE];
    Ltable *Tlink;
    struct word_node *link;

}Wlist;

int to_create_list_of_files(Flist **f_head, char *name);

void create_database(Flist *f_head, Wlist *head[]);

// read contents of a file
Wlist *read_datafile(Wlist *head[], char *filename);
// create word_list
int insert_at_last(Wlist **head, data_t *data);

// update linktable
int update_link_table(Wlist **head);

// update word count
int update_word_count(Wlist **head, char *file_name);

// print_word_count
int print_word_count(Wlist *head);

//searching a word
int search(Wlist *head, char *word);

//display
void display_database(Wlist *head[]);

//save_databse
int save_database(Wlist *head[]);

//write in to file
void write_databasefile(Wlist *head, FILE *datafile);

//update
int update_database(Wlist *head[], Flist **f_head);

int isFileEmpty(char *filename);

int file_validation_n_file_list(Flist **f_head, char *argv[]);

int hash_function(const char *word);

int isfileexist(char *filename);
#endif