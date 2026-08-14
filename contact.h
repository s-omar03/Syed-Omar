#ifndef CONTACT_H //preprocessor directive ensuring its run only once
#define CONTACT_H //used to define macro(const name)

#define MAX_CONTACTS 100

typedef struct { 
    char name[50];
    char phone[20];
    char email[50];
} Contact;//due to typedef its struct datatype

typedef struct {
    Contact contacts[100];
    int contactCount; //num of contacts added
} AddressBook; //AddressBook var_name;

void createContact(AddressBook *addressBook);
void searchContact(AddressBook *addressBook);
void editContact(AddressBook *addressBook);
void deleteContact(AddressBook *addressBook);
void listContacts(AddressBook *addressBook);
void initialize(AddressBook *addressBook);
void saveContactsToFile(AddressBook *addressBook);

#endif 
