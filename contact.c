#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "contact.h"
#include "file.h"
#include "populate.h"

void listContacts(AddressBook *addressBook) 
{
    // Sort contacts based on the chosen criteria
    for(int i=0;i<addressBook->contactCount;i++){
        printf("%s\t%s\t%s\n",addressBook->contacts[i].name,addressBook->contacts[i].phone,addressBook->contacts[i].email);
    }
}

void initialize(AddressBook *addressBook) {
    //initialize adb with dummy contacts
    loadContactsFromFile(addressBook);
    if(addressBook->contactCount == 0)
        populateAddressBook(addressBook);
    //to initialize adb with data from file

}

void saveAndExit(AddressBook *addressBook) {
    saveContactsToFile(addressBook);
}


void createContact(AddressBook *addressBook)
{
	/* Define the logic to create a Contact*/
    printf("Enter contact name: ");
    scanf(" %[^\n]",addressBook->contacts[addressBook->contactCount].name);
    printf("Enter contact phone number: ");
    scanf(" %s",addressBook->contacts[addressBook->contactCount].phone);
    printf("Enter contact email address: ");
    scanf(" %s",addressBook->contacts[addressBook->contactCount].email);
    addressBook->contactCount++;
}
/* Define the logic for search 
    use strcmp to compare name*/
void searchContact(AddressBook *addressBook)
{
    int choice, i, flag = 0;
    char key[50];

    printf("Search by:\n");
    printf("1. Name\n");
    printf("2. Phone\n");
    printf("3. Email\n");
    printf("Enter choice: ");
    scanf("%d", &choice);

    printf("Enter value to search: ");
    scanf(" %[^\n]", key);

    for(i = 0; i < addressBook->contactCount; i++)
    {
        if(choice == 1 && strcmp(key, addressBook->contacts[i].name) == 0){
            flag = 1;
            break;
        }
        else if(choice == 2 && strcmp(key, addressBook->contacts[i].phone) == 0){
            flag = 1;
            break;
        }
        else if(choice == 3 && strcmp(key, addressBook->contacts[i].email) == 0){
            flag = 1;
            break;
        }
    }

    if(flag)
        printf("%s\t%s\t%s\n",
               addressBook->contacts[i].name,
               addressBook->contacts[i].phone,
               addressBook->contacts[i].email);
    else
        printf("Contact not found.");
}

void editContact(AddressBook *addressBook)
{
    int choice, i, flag = 0;
    char key[50], newValue[50];

    printf("Search by:\n");
    printf("1. Name\n");
    printf("2. Phone\n");
    printf("3. Email\n");
    printf("Enter choice: ");
    scanf("%d", &choice);

    printf("Enter value to search: ");
    scanf(" %[^\n]", key);

    for(i = 0; i < addressBook->contactCount; i++)
    {
        if(choice == 1 && strcmp(key, addressBook->contacts[i].name) == 0){
            flag = 1;
            break;
        }
        else if(choice == 2 && strcmp(key, addressBook->contacts[i].phone) == 0){
            flag = 1;
            break;
        }
        else if(choice == 3 && strcmp(key, addressBook->contacts[i].email) == 0){
            flag = 1;
            break;
        }
    }

    if(flag){
        printf("Current: %s\t%s\t%s\n",
               addressBook->contacts[i].name,
               addressBook->contacts[i].phone,
               addressBook->contacts[i].email);

        printf("Enter new value: ");
        scanf(" %[^\n]", newValue);

        if(choice == 1)
            strcpy(addressBook->contacts[i].name, newValue);
        else if(choice == 2)
            strcpy(addressBook->contacts[i].phone, newValue);
        else if(choice == 3)
            strcpy(addressBook->contacts[i].email, newValue);

        printf("Updated: %s\t%s\t%s\n",
               addressBook->contacts[i].name,
               addressBook->contacts[i].phone,
               addressBook->contacts[i].email);
    }
    else
        printf("Contact not found.");
}
void deleteContact(AddressBook *addressBook)
{
    /* Define the logic for deletecontact */
    int choice, i, j, flag = 0;
    char key[50];

    printf("Delete by:\n");
    printf("1. Name\n");
    printf("2. Phone\n");
    printf("3. Email\n");
    printf("Enter choice: ");
    scanf("%d", &choice);

    printf("Enter value to search: ");
    scanf(" %[^\n]", key);

    for(i = 0; i < addressBook->contactCount; i++)
    {
        if(choice == 1 && strcmp(key, addressBook->contacts[i].name) == 0){
            flag = 1;
            break;
        }
        else if(choice == 2 && strcmp(key, addressBook->contacts[i].phone) == 0){
            flag = 1;
            break;
        }
        else if(choice == 3 && strcmp(key, addressBook->contacts[i].email) == 0){
            flag = 1;
            break;
        }
    }

    if(flag){
        for(j = i; j < addressBook->contactCount - 1; j++){
            addressBook->contacts[j] = addressBook->contacts[j + 1];
        }
        addressBook->contactCount--;
        printf("Contact deleted.\n");
    }
    else
        printf("Contact not found.");
}
