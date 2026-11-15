#include "inverted_Search.h"

char *filename;

void create_database(Flist *f_head, Wlist *head[])
{
    while (f_head)
    {
        //read each file one after the other
        read_datafile(head, f_head->file_name);
        //move the file head to next node
        f_head = f_head->link;
    }
}

//function to read the content of the file and create database
Wlist *read_datafile(Wlist *head[], char *file_name)
{
    //open the file in read mode
    FILE *fptr = fopen(file_name, "r");
    if (fptr == NULL)
    {
        printf("Unable to open file %s\n", file_name);
        return NULL;
    }

    char word[WORD_SIZE];
    filename = file_name; // used by update_link_table for new words

    //reading the word till EOF
    while (fscanf(fptr, "%29s", word) != EOF)
    {
        int flag = 1; // reset for every word

        //find the index of the word
        int index = tolower((unsigned char)word[0]) % 97;
        //check the range of the word
        if (!(index >= 0 && index <= 25))
        {
            //numbers and symbols
            index = 26;
        }
        if (head[index] != NULL)
        {
            Wlist *temp = head[index];
            //traversing and checking for repeated word
            while (temp)
            {
                if (!strcmp(temp->word, word))
                {
                    update_word_count(&temp, file_name);
                    flag = 0;
                    break;
                }
                temp = temp->link;
            }
        }
        //if not a duplicate word
        if (flag == 1)
        {
            insert_at_last(&head[index], word);
        }
    }

    fclose(fptr);
    return NULL;
}

int update_word_count(Wlist **head, char *file_name)
{
    Ltable *temp = (*head)->Tlink;
    Ltable *prev = NULL;

    while (temp)
    {
        if (!strcmp(temp->file_name, file_name))
        {
            temp->word_count++;
            return SUCCESS;
        }
        prev = temp;
        temp = temp->table_link;
    }

    Ltable *new_entry = malloc(sizeof(Ltable));
    if (new_entry == NULL)
    {
        return FAILURE;
    }
    new_entry->word_count = 1;
    strncpy(new_entry->file_name, file_name, FNAME_SIZE - 1);
    new_entry->file_name[FNAME_SIZE - 1] = '\0';
    new_entry->table_link = NULL;

    if (prev == NULL)
    {
        (*head)->Tlink = new_entry;
    }
    else
    {
        prev->table_link = new_entry;
    }

    (*head)->file_count++;
    return SUCCESS;
}