#include "inverted_Search.h"

int save_database(Wlist *head[])
{
    char file_name[FNAME_SIZE];

    printf("Enter the filename: ");
    scanf("%29s", file_name);

    //open the file
    FILE *fptr = fopen(file_name, "w");
    if (fptr == NULL)
    {
        printf("Unable to open file %s for writing\n", file_name);
        return FAILURE;
    }

    //array of heads
    for (int i = 0; i < TABLE_SIZE; i++)
    {
        //check if the list is empty or not
        if (head[i] != NULL)
        {
            write_databasefile(head[i], fptr);
        }
    }

    fclose(fptr);
    printf("Database saved successfully to %s\n", file_name);
    return SUCCESS;
}

void write_databasefile(Wlist *head, FILE *datafile)
{
    /*
    #[0] : [word] : [file_count] : filename : [word count]
                                    filename : [word count]
    */
    while (head != NULL)
    {
        fprintf(datafile, "#[%d]\t[%s]\t%d\n",
                tolower((unsigned char)head->word[0]) % 97, head->word, head->file_count);

        Ltable *Thead = head->Tlink;
        while (Thead)
        {
            fprintf(datafile, "\t\t\t\t%s\t%d\n", Thead->file_name, Thead->word_count);
            Thead = Thead->table_link;
        }
        head = head->link;
    }
}