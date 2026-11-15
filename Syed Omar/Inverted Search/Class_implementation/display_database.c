#include "inverted_Search.h"

void display_database(Wlist *head[])
{
    printf("%-6s %-14s %-12s %-12s %s\n", "[idx]", "[word]", "file_count", "file_name", "word_count");
    for (int i = 0; i < TABLE_SIZE; i++)
    {
        if (head[i] != NULL)
        {
            print_word_count(head[i]);
        }
    }
}

// print the word count
int print_word_count(Wlist *head)
{
    while (head != NULL)
    {
        printf("[%-3d] [%-12s] %-12d", tolower((unsigned char)head->word[0]) % 97, head->word, head->file_count);
        Ltable *Thead = head->Tlink;
        while (Thead)
        {
            printf("%-12s %-6d", Thead->file_name, Thead->word_count);
            Thead = Thead->table_link;
        }
        printf("\n");
        head = head->link;
    }
    return SUCCESS;
}