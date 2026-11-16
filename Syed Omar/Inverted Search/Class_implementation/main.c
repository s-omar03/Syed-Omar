#include "inverted_Search.h"

/* Free every node of the file list and of the word database */
static void free_everything(Flist *f_head, Wlist *head[])
{
    while (f_head != NULL)
    {
        Flist *next = f_head->link;
        free(f_head);
        f_head = next;
    }

    for (int i = 0; i < TABLE_SIZE; i++)
    {
        Wlist *w = head[i];
        while (w != NULL)
        {
            Wlist *next_word = w->link;
            Ltable *t = w->Tlink;
            while (t != NULL)
            {
                Ltable *next_table = t->table_link;
                free(t);
                t = next_table;
            }
            free(w);
            w = next_word;
        }
        head[i] = NULL;
    }
}

int main(int argc, char *argv[])
{
    system("clear");

    // validation of CLA
    if (argc < 2)
    {
        printf("Enter the valid number of arguments\n");
        printf("./inverted.exe file1.txt file2.txt file3.txt ...\n");
        return 0;
    }

    // create the file linked list -- collection of files
    Flist *f_head = NULL;
    Wlist *head[TABLE_SIZE] = {NULL};

    // validation of CLA
    file_validation_n_file_list(&f_head, argv);
    if (f_head == NULL)
    {
        printf("No files have been added to the file linked list\n");
        printf("Hence the process got terminated\n");
        return 1;
    }

    int choice;
    do
    {
        // Display the menu
        printf("\n1. Create Database\n");
        printf("2. Display Database\n");
        printf("3. Search Database\n");
        printf("4. Update Database\n");
        printf("5. Save Database\n");
        printf("6. Exit\n");
        printf("Enter a Choice: ");
        if (scanf("%d", &choice) != 1)
        {
            /* Not a number (or end of input): discard the line, or stop at EOF */
            int c;
            while ((c = getchar()) != '\n' && c != EOF)
                ;
            if (c == EOF)
                break;
            choice = 0;
        }

        switch (choice)
        {
        case 1:
            create_database(f_head, head);
            break;
        case 2:
            display_database(head);
            break;
        case 3:
        {
            char word[WORD_SIZE];
            printf("Enter the word to search: ");
            scanf("%29s", word);
            int index = tolower((unsigned char)word[0]) % 97;
            if (!(index >= 0 && index <= 25))
            {
                index = 26;
            }
            search(head[index], word);
            break;
        }
        case 4:
            update_database(head, &f_head);
            break;
        case 5:
            save_database(head);
            break;
        case 6:
            printf("Exiting...\n");
            break;
        default:
            printf("Invalid choice\n");
            break;
        }
    } while (choice != 6);

    free_everything(f_head, head);
    return 0;
}