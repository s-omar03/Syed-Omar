# Inverted Search (Data Structures)

A menu-driven **inverted-search database** written in C using only linked lists. It records which words appear in which files and how many times, so a word can be searched across several files at once.

(The multi-threaded, Linux-internals version of this idea is in the `Multi Threaded Inverted File Index` project.)

## How it works
1. **File list** – the file names given on the command line are validated: files that are missing, empty or repeated are rejected. The valid ones are kept in a linked list.
2. **Database** – a hash table of **27 buckets**: one for each letter `a–z`, plus one for numbers and symbols. A word goes into the bucket for its first letter.
3. **Word nodes** – each word node holds a count of files and a sub-list of the files it appears in, with the number of times it occurs in each.

## Menu
```
1. Create Database
2. Display Database
3. Search Database
4. Update Database   (add another file)
5. Save Database     (write to a file)
6. Exit
```

## Build and run
```bash
make
./inverted file1.txt file2.txt file3.txt
```
Sample input files (`file1.txt` … `file4.txt`) are included. Run `make clean` to remove build output.

## Files
| File | Purpose |
|------|---------|
| `main.c` | Menu loop |
| `file_validation.c` | Checks each file and builds the file list |
| `create_database.c`, `insert_last.c` | Reads words into the hash table |
| `display_database.c`, `search.c` | Display and word lookup |
| `update_database.c`, `save_database.c` | Add a file, save to a file |
| `inverted_Search.h` | Structures and prototypes |

## Known limitations
- Search is case-sensitive.

## Concepts used
C, singly linked lists, hash table, dynamic memory, file I/O, Makefile.
