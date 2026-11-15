#include "inverted_Search.h"

int search(Wlist *head, char *word)
{
    //check the list empty or not
    if (head == NULL)
    {
        printf("List is empty\n");
        return FAILURE;
    }
    while (head)
    {
        //compare the node word with the input word
        if (!(strcmp(head->word, word)))
        {
            printf("Word %s is present in %d files\n", head->word, head->file_count);
            Ltable *Thead = head->Tlink;
            while (Thead)
            {
                printf("In file: %s  %d\n", Thead->file_name, Thead->word_count);
                Thead = Thead->table_link;
            }
            printf("\n");
            return SUCCESS;
        }
        head = head->link;
    }
    printf("Search word is not found\n");
    return FAILURE;
}