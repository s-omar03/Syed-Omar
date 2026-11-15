# Arbitrary Precision Calculator (APC)

A command-line calculator in C that works on integers of any length, beyond the 64-bit limit. Each number is stored digit by digit in a **doubly linked list**, and the arithmetic is done on the lists the way it is done on paper.

## Features
- Addition, subtraction, multiplication and division
- Negative numbers, with correct handling of signs
- Division by zero is detected and reported
- Result of `0` never prints a minus sign

## Build and run
```bash
make
./apc <number1> <operator> <number2>
```
Quote `*` so the shell does not expand it.

```bash
./apc 999 + 1          # 1000
./apc 20 - 50          # -30
./apc 99 '*' 99        # 9801
./apc -10 '*' -5       # 50
./apc 100 / 4          # 25
make test              # runs sample cases
```

## Files
| File | Purpose |
|------|---------|
| `main.c` | Reads arguments, chooses the operation, applies the sign rules |
| `apc.c` / `apc.h` | List helpers: build a list from digits, compare, insert, print, delete |
| `addition.c`, `subtraction.c`, `multiplication.c`, `division.c` | One file per operation |

## Concepts used
C, doubly linked lists, dynamic memory, command-line arguments, Makefile.
