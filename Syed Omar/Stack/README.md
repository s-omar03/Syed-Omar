# Stack Data Structure (C++)

A templated, array-based **stack (LIFO)** class in C++ with automatic growth, and a menu-driven demo program.

## Features
- `Stack<T>` works with any type
- Starts with capacity 10 and **doubles** when full
- `push`, `pop`, `peek`, `isEmpty`, `size`, `clear`
- `pop` on an empty stack throws `std::underflow_error`
- Follows the **rule of three**: destructor, copy constructor and copy assignment, so copies do not share memory

## Build and run
```bash
g++ -std=c++11 -Wall -Wextra main.cpp -o stack_demo
./stack_demo
```

## Demo menu
```
1. Push   2. Pop   3. Peek   4. Is Empty?   5. Size   6. Clear   0. Exit
```

## Files
| File | Purpose |
|------|---------|
| `Stack.h` | The `Stack<T>` template class |
| `main.cpp` | Menu-driven demo using `Stack<int>` |

## Concepts used
C++ templates, dynamic memory management, exception handling, rule of three.
