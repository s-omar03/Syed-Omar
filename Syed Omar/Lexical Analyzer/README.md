# Lexical Analyzer for C

A lexical analyzer in C. It reads a C source file character by character and prints each token with its type.

## Token types recognised
Preprocessor directives, keywords, identifiers, numbers (including float and hex literals), string literals, operators (including two-character ones such as `==`, `!=`, `<=`, `>=`, `++`, `--`), parentheses, braces, brackets, semicolons and commas. It also reports unmatched or unterminated constructs, such as an open brace or a string that is never closed.

## Build and run
```bash
gcc main.c analyser.c -o lexer
./lexer test_program.c
```
Only `.c` files are accepted. `test_program.c` is a sample input.

## Example output format
```
KEYWORD            -> int
IDENTIFIER         -> main
OPEN PARENTHESIS   -> (
```

## Files
| File | Purpose |
|------|---------|
| `main.c` | Checks arguments and starts the analysis |
| `analyser.c`, `analyser.h` | Tokeniser and token classification |
| `test_program.c` | Sample C file to analyse |

## Concepts used
C, file handling, character classification, lexical analysis, string handling.
