#include <stdio.h>
#include "contact.h"
int main() {
    int choice;
    AddressBook adb;
    initialize(&adb);
    do 
    {
        printf("\nAddress Book Menu:\n");
        printf("1. Create contact\n");
        printf("2. Search contact\n");
        printf("3. Edit contact\n");
        printf("4. Delete contact\n");
        printf("5. List all contacts\n");
        printf("6. Exit\n");
        printf("Enter your choice: ");
        scanf("%d", &choice);

        switch (choice) 
        {
            case 1:
                createContact(&adb);
                break;
            case 2:
                searchContact(&adb);
                break;
            case 3:
                editContact(&adb);
                break;
            case 4:
                deleteContact(&adb);
                break;
            case 5:
                listContacts(&adb);
                break;
            case 6:
                printf("Saving and Exiting...\n");
                saveContactsToFile(&adb);
                break;
            default:
                printf("Invalid choice. Please try again.\n");
        }
    } while (choice != 6);
    return 0;
}
