# source2html – C Source to HTML Converter

Converts a C source file into a **syntax-highlighted HTML page**. A finite state machine reads the source and decides, character by character, whether it is a comment, string, keyword, number and so on. Each type of token gets its own colour in the output.

## Build and run
```bash
make
./source2html path/to/file.c      # writes output.html
make test                          # runs the checks in tests/
make clean
```
Open `output.html` in a browser to see the result. The page uses a dark theme with a CSS style block.

## What the tests check (`tests/test.sh`)
- `output.html` is created
- A string with an escaped quote is not corrupted
- Float and hex literals (`3.14159f`, `0xFF`) are kept as one token

## Project layout
```
source2html/
├── include/   parser.h, output.h
├── src/       main.c, parser.c, output.c
└── tests/     test.c, test.sh
```

## Concepts used
C, finite state machines, parsing, callbacks, dynamic memory, file I/O, shell test script, Makefile.
