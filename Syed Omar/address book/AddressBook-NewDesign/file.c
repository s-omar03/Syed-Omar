#include <stdio.h>
#include <string.h>
#include "file.h"

void saveContactsToFile(AddressBook *addressBook)
{
    FILE *fp = fopen("contacts.csv", "w");
    if(fp == NULL)
        return;
    for(int i = 0; i < addressBook->contactCount; i++)
        fprintf(fp, "%s,%s,%s\n",
                addressBook->contacts[i].name,
                addressBook->contacts[i].phone,
                addressBook->contacts[i].email);

    fclose(fp);
}

void loadContactsFromFile(AddressBook *addressBook)
{
    FILE *fp = fopen("contacts.csv", "r");
    if(fp == NULL)
        return;

    addressBook->contactCount = 0;

    while(!feof(fp))
    {
        Contact c;
        if(fscanf(fp, " %[^,],%[^,],%[^\n]\n",
                  c.name, c.phone, c.email) == 3)
        {
            addressBook->contacts[addressBook->contactCount++] = c;
        }
    }

    fclose(fp);
}