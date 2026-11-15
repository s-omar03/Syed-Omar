# Multi-Threaded Inverted File Index

A text search engine written in C. `./search index` reads text files in
parallel and builds an *inverted index*: for every word, the list of files
and character offsets where it occurs. The index is saved to `index.dat`.
`./search search <word>` loads that index and lists every occurrence of the
word, ignoring case.

The full assignment is in [PROJECT_SPEC.md](PROJECT_SPEC.md).

## Building

Needs Linux (or another POSIX system) with GCC 4.9 or newer and pthreads.

```
make          # builds ./search
make test     # runs the automated tests (73 checks)
make bench    # benchmark on 100 generated files of ~1 MB
make clean    # removes the binary, object files and index.dat
```

`make asan` and `make tsan` rebuild with AddressSanitizer or
ThreadSanitizer; run `make test` afterwards to test that build.

## Usage

```
./search index [-t <threads>] <file1> <file2> ...   # create index.dat
./search search [-v] <word>                         # search for a word
./search delete                                     # delete index.dat
```

Example with the two sample files from the spec:

```
$ ./search index sample1.txt sample2.txt
Indexing 2 files using 2 threads...
Thread 124638565947072 indexing sample1.txt
Thread 124638557554368 indexing sample2.txt
Indexing complete. Index saved to index.dat
Elapsed time: 0.000792 seconds (threads=2, files=2)

$ ./search search SUCCESS
sample1.txt at position 79
sample2.txt at position 44

$ ./search search -v the
sample1.txt at position 0
sample1.txt at position 17
sample1.txt at position 38
sample2.txt at position 8

Found 4 occurrence(s) in 2 file(s).
Index load time: 0.000040 seconds (61 words, 2 files)
Search time:     0.000000 seconds

$ ./search delete
Index file 'index.dat' deleted successfully.
```

How it behaves:

- **Threads:** 8 by default; `-t` accepts 1 to 256. It never starts more
  threads than there are files.
- **Positions** are 0-based character (byte) offsets from the start of the
  file, as the spec defines `FilePosition.position`. In `sample1.txt`,
  "success" starts at offset 79. The spec's sample output shows 60 and 8
  for this search, but those numbers don't fit the sample files under any
  way of counting, so the documented definition was used.
- **Words** are runs of letters and digits, stored in lowercase.
  Punctuation separates words, so `don't` is indexed as `don` and `t`.
  Bytes of 0x80 and above count as letters, so UTF-8 words stay whole, but
  only A-Z are converted to lowercase. Words longer than 256 characters are
  cut to 256.
- **Search terms** are normalized the same way, so `"Success."` finds
  `success`. A term containing two words is rejected.
- **Results** are ordered by file (in command-line order), then by position.
- Every `index` run **replaces** `index.dat`. The new index is written to
  `index.dat.tmp` and then renamed, so a failed run never damages the old
  index.
- A file name given twice is indexed once. An unreadable file produces a
  warning; the index is still saved if at least one file was indexed.
- The exit status is 0 on success (including "no matches") and 1 on any
  error.

## Files

| File | Contents |
|---|---|
| `main.c` | Command line, thread pool, timing |
| `hash_table.c`, `hash_table.h` | Thread-safe hash table: word → occurrences |
| `index.c`, `index.h` | Tokenizer, index builder, saving and loading `index.dat` |
| `search.c`, `search.h` | Query normalization, lookup, result printing |
| `makefile` | Build, test, benchmark and sanitizer targets |
| `sample1.txt`, `sample2.txt` | Sample input from the spec |
| `tests/run_tests.sh` | Automated tests |
| `tests/benchmark.sh` | Benchmark |
| `tests/gen_corpus.sh` | Generates synthetic documents for the tests and benchmark |
| `PROJECT_SPEC.md` | The assignment |

## Design

### Data structures

The structures are the ones given in the spec. The only change is that
`FilePosition.filename` is `const char *`: every occurrence points at one
shared copy of the file name instead of duplicating it.

```
HashTable.buckets[]
   │
   ├─► HashNode "success" ─► HashNode "time" ─► NULL        (collision chain)
   │        │ positions
   │        ▼
   │     FilePosition {sample1.txt, 79} ─► FilePosition {sample2.txt, 44} ─► NULL
   ⋮
```

### Hash table (`hash_table.c`)

- Separate chaining, FNV-1a hash, bucket count always a power of two.
- The table doubles in size when the load factor goes above 0.75.
- Thread safety comes from **lock striping**. The table has 64 mutexes, and
  a word is protected by the mutex selected by the low 6 bits of its hash.
  The bucket count is a power of two and at least 64, so all words in one
  bucket share a mutex whatever the table size is.
  - Insert and lookup lock one mutex, so threads working on different
    words rarely wait for each other.
  - Resizing locks all 64 mutexes, always in the same order.
  - No thread ever waits for a mutex while holding another, so the table
    cannot deadlock.

### Index builder (`index.c`) and thread pool (`main.c`)

1. `main.c` starts the worker threads. Each worker takes the next file
   number from a mutex-protected counter. Files are handed out one at a
   time, so one large file does not hold up the others.
2. A worker reads its file in 64 KB chunks, lowercases each word, and
   records every occurrence in a private hash table for that file. Nothing
   is shared at this stage, so there is no lock contention.
3. When the file is finished, the private table is merged into the shared
   table. The worker takes one lock per distinct word in the file instead
   of one per occurrence. For the benchmark's 1 MB files that is about
   8,000 lock operations per file instead of about 170,000.
4. Each word's list is merged as one block, so the occurrences of a word
   from one file stay together and in ascending order. Saving relies on
   this, so it never has to sort individual occurrences.
5. A file that fails partway through is dropped completely; nothing from it
   reaches the index.

There is one `FilePosition` per word occurrence, millions for a large
collection, so they are not allocated with one `malloc` each. They come
from memory blocks that start at 64 entries and double up to 4,096. This
makes indexing faster, and freeing the index takes one `free` per block.

### `index.dat`

A compact binary file:

```
"IIDX"  version  file_count  word_count
file table:      for each file:  name_length  name
words, sorted:   for each word:  word_length  word  file_count
                                 for each file:  file_id  count  position deltas...
"IEND"
```

- All numbers are variable-length integers (LEB128). Positions are stored
  as the difference from the previous position in the same file, so most
  take 1 or 2 bytes.
- The same input always produces a byte-identical `index.dat`, whatever the
  thread count. The tests check this, which makes it a good test of the
  thread safety.
- Saving also uses the worker threads: the sorted words are split into
  chunks that the threads encode in parallel, and the chunks are written in
  order.
- Loading checks every length, count and file id, and the start and end
  markers. A damaged or truncated file gives an error message instead of a
  crash.

### Search (`search.c`)

`search` loads `index.dat` into a hash table sized for its word count,
normalizes the query exactly as the indexer does, and finds the word with
one O(1) hash lookup. `-v` prints the load time and the lookup time
separately.

## Testing

`make test` runs 73 checks in a temporary directory:

- the spec's examples and error messages;
- every reported position is read back from the file to confirm the word is
  there, and the number of results is compared with `grep -o -i -w`;
- command-line errors, missing or unreadable files, directories, duplicate
  file names;
- edge cases: an empty file, a file without a final newline, numbers, UTF-8
  words, a 300-character word, a file name with a space;
- thread safety: a 24-file collection indexed with 1, 2, 3 and 8 threads
  must give byte-identical `index.dat` files;
- damaged indexes: truncated, foreign and empty files, plus one copy per
  damaged byte, with a single byte overwritten at every third offset. The
  search must never crash.

All 73 checks also pass with the AddressSanitizer + UndefinedBehaviorSanitizer
build (`make asan`) and the ThreadSanitizer build (`make tsan`).
ThreadSanitizer reported no data races.

## Benchmark

`make bench` on 100 generated files of about 1 MB each (100 MB in total,
17 million word occurrences), 12-core machine, GCC 14:

| Threads | Indexing time (build + save) |
|---:|---:|
| 1 | 5.52 s |
| 2 | 2.81 s |
| 4 | 1.62 s |
| 8 | 1.26 s |

| Measure | Result | Spec's figure |
|---|---|---|
| Indexing, 8 threads | 1.26 s | 2.54 s |
| Hash table lookup | 0.000002 s | < 0.001 s |
| Loading `index.dat` (done once per `search` command) | 0.32 s | not given |
| `index.dat` size | 39.8 MB | ~15 MB |
| Peak memory while indexing | 450 MB | ~45 MB |

The spec's memory and file-size figures cannot be reached with the data
structures it requires. They store one 24-byte `FilePosition` per
occurrence, which comes to about 400 MB for 17 million occurrences before
anything else is counted.

## Limitations

- There is one `index.dat`, in the current directory. `index` replaces it;
  files cannot be added to an existing index.
- Searches are for one word at a time; phrase searches are not supported.
- Only ASCII letters are case-folded.
- Thread IDs are printed as `unsigned long`, which matches `pthread_t` on
  Linux.
