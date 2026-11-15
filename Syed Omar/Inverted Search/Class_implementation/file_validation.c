#include "inverted_Search.h"

int file_validation_n_file_list(Flist **f_head, char *argv[])
{
    int i = 1, empty; // argv[0] is the executable name, exempted
    while (argv[i] != NULL)
    {
        empty = isFileEmpty(argv[i]);
        if (empty == FILE_NOTAVAILABLE)
        {
            printf("File %s is not available\n", argv[i]);
            printf("Hence we are not adding that file to database\n");
        }
        else if (empty == FILE_EMPTY)
        {
            printf("File %s is empty or not having any content\n", argv[i]);
            printf("Hence we are not adding that file to database\n");
        }
        else
        {
            int ret_val = to_create_list_of_files(f_head, argv[i]);
            if (ret_val == SUCCESS)
            {
                printf("Successfully inserted %s file into file linked list\n", argv[i]);
            }
            else if (ret_val == REPEATATION)
            {
                printf("File %s already present in the linked list so not added again\n", argv[i]);
            }
            else
            {
                //if any other error
                printf("Unexpected error while adding %s\n", argv[i]);
            }
        }
        i++; // always advance - previously missing on the success path, which looped forever
    }
    return SUCCESS;
}

// fun is to check file availability and for file contents
int isFileEmpty(char *filename)
{
    FILE *fptr = fopen(filename, "r");
    if (fptr == NULL)
    {
        // any failure to open (missing, no permission, etc.) means unavailable
        return FILE_NOTAVAILABLE;
    }

    fseek(fptr, 0, SEEK_END);
    long size = ftell(fptr);
    fclose(fptr);

    if (size == 0)
    {
        return FILE_EMPTY;
    }
    return SUCCESS;
}

// fun() is to create file linked list (insert_last())
int to_create_list_of_files(Flist **f_head, char *name)
{
    // check for duplicate
    Flist *temp = *f_head;
    while (temp)
    {
        if (!strcmp(temp->file_name, name))
        {
            return REPEATATION;
        }
        temp = temp->link;
    }

    // create a new node
    Flist *new_node = malloc(sizeof(Flist));
    if (new_node == NULL)
    {
        return FAILURE;
    }
    strncpy(new_node->file_name, name, FNAME_SIZE - 1);
    new_node->file_name[FNAME_SIZE - 1] = '\0';
    new_node->link = NULL;

    // insert at the end of the list
    if (*f_head == NULL)
    {
        *f_head = new_node;
        return SUCCESS;
    }

    temp = *f_head;
    while (temp->link)
    {
        temp = temp->link;
    }
    temp->link = new_node;
    return SUCCESS;
}