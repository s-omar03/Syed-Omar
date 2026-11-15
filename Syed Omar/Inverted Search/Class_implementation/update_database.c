#include "inverted_Search.h"

int update_database(Wlist *head[], Flist **f_head)
{
    //prompt the user to enter the file
    char file_name[FNAME_SIZE];
    printf("Enter the filename to update the database: ");
    scanf("%29s", file_name);

    // validation of the given file -- available, has content
    int status = isFileEmpty(file_name);
    if (status == FILE_NOTAVAILABLE)
    {
        printf("File %s is not available\n", file_name);
        return FAILURE;
    }
    if (status == FILE_EMPTY)
    {
        printf("File %s is empty\n", file_name);
        return FAILURE;
    }

    // search for the file in the existing list using a temp pointer
    Flist *temp = *f_head;
    Flist *match = NULL;
    while (temp)
    {
        if (!strcmp(temp->file_name, file_name))
        {
            match = temp;
            break;
        }
        temp = temp->link;
    }

    if (match != NULL)
    {
        // file already tracked -- re-read it to refresh word/file counts
        read_datafile(head, file_name);
        printf("Database updated for file %s\n", file_name);
    }
    else
    {
        // new file -- add it to the file list, then read it in
        int ret_val = to_create_list_of_files(f_head, file_name);
        if (ret_val == SUCCESS)
        {
            read_datafile(head, file_name);
            printf("File %s added and database updated\n", file_name);
        }
        else
        {
            printf("Could not add file %s to the database\n", file_name);
            return FAILURE;
        }
    }

    return SUCCESS;
}