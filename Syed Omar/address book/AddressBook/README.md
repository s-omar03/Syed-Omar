# Address Book

A menu-driven contact manager in C. Contacts are kept in memory while the program runs and are saved to a **CSV file**, so they are loaded again the next time it starts.

## Menu
```
1. Create contact
2. Search contact
3. Edit contact
4. Delete contact
5. List all contacts
6. Exit
```
Each contact has a name, phone number and email address. Input is checked before it is accepted.

## Build and run
```bash
gcc *.c -o addressbook
./addressbook
```
`contacts.csv` holds the saved contacts (one per line: `name,phone,email`). Sample contacts are included.

## Files
| File | Purpose |
|------|---------|
| `main.c` | Menu loop |
| `contact.c`, `contact.h` | Create, search, edit, delete and list operations |
| `file.c`, `file.h` | Load from and save to the CSV file |
| `populate.c`, `populate.h` | Loads the contacts at start-up |

## Concepts used
C, structures, arrays, file I/O (CSV), string handling, input validation.
