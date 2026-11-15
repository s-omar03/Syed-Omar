#include "inverted_Search.h"

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
        scanf("%d", &choice);

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

    return 0;
}