# Red-Black Tree Project

A complete, from-scratch **Red-Black Tree** implementation in C, with a
command-line interface for interacting with the tree.

A Red-Black Tree is a self-balancing binary search tree. Each node carries
an extra "color" bit (red or black), and a small set of invariants keeps the
tree approximately balanced after every insert/delete, guaranteeing
**O(log n)** height.

## Properties maintained

In addition to standard BST ordering, this implementation enforces:

1. Every node is either **RED** or **BLACK**.
2. The root is always **BLACK**.
3. Every leaf (represented internally by a shared `NIL` sentinel) is **BLACK**.
4. A **RED** node never has a **RED** child (no two reds in a row on any path).
5. Every path from a node to any of its descendant NIL leaves contains the
   same number of black nodes (the **black-height**).

## Operations implemented

| Operation        | Function              | Complexity  |
|------------------|------------------------|-------------|
| Insertion        | `rb_insert`            | O(log n)    |
| Deletion         | `rb_delete_value` / `rb_delete_node` | O(log n) |
| Search           | `rb_search`             | O(log n)    |
| Find Minimum     | `rb_find_min`           | O(log n)    |
| Delete Minimum   | `rb_delete_min`         | O(log n)    |
| Find Maximum     | `rb_find_max`           | O(log n)    |
| Delete Maximum   | `rb_delete_max`         | O(log n)    |

Insertions and deletions perform the standard fix-up routines (rotations +
recoloring) to restore all five properties after the underlying BST
operation. A `rb_validate` routine is also included, which walks the tree
and confirms every property holds — useful for testing after any sequence
of operations.

## Files

```
rbtree_project/
├── rbtree.h      # Public API: struct/enum defs + function declarations
├── rbtree.c      # Implementation: rotations, insert/delete + fix-ups,
│                 # search, min/max, printing, validation
├── main.c        # Interactive CLI menu only — includes rbtree.h
├── Makefile      # `make` to build, `make run` to build+run, `make clean`
└── README.md     # This file
```

The data structure is split from the interface on purpose:

- **`rbtree.h` / `rbtree.c`** is a self-contained, reusable module. It has
  no `main()` and no CLI code, so it could be dropped into any other C
  program (or linked into a test harness) just by including `rbtree.h`.
- **`main.c`** contains *only* the menu loop and input handling; every call
  it makes (`rb_insert`, `rb_delete_value`, `rb_print`, etc.) goes through
  the public API declared in `rbtree.h`.

Within `rbtree.c` itself, code is still organized top-to-bottom for
readability: construction → rotations → insertion (+fixup) →
search/min/max → deletion (+fixup) → printing → validation → cleanup.

## Building & running

Requires only a C compiler (gcc or clang) — no external dependencies.

```bash
make          # compiles rbtree.c and main.c separately, links ./rbtree
./rbtree      # run it
# or simply:
make run
```

`make clean` removes the compiled binary and object files
(`rbtree.o`, `main.o`, `rbtree`).

Or compile directly:

```bash
gcc -Wall -Wextra -std=c11 -c rbtree.c
gcc -Wall -Wextra -std=c11 -c main.c
gcc -Wall -Wextra -std=c11 -o rbtree rbtree.o main.o
./rbtree
```

## Using the CLI

On launch you'll see a menu:

```
============ RED-BLACK TREE MENU ============
 1. Insert a value
 2. Delete a value
 3. Search for a value
 4. Find Minimum
 5. Delete Minimum
 6. Find Maximum
 7. Delete Maximum
 8. Display tree (structured view)
 9. Display tree (in-order view)
10. Validate red-black properties
 0. Exit
===============================================
Enter your choice:
```

### Insert

Choosing **1** and entering a value shows the tree **before** insertion,
performs the insert, runs the fix-up rotations/recoloring, then shows the
tree **after** — so you can see exactly how rebalancing changed the shape
and colors.

### Delete

Choosing **2** works the same way for deletion: before/after views, with
the delete fix-up applied automatically. Options **5** and **7** do the
same for deleting the current minimum and maximum, respectively.

### Search / Min / Max

Options **3**, **4**, and **6** report whether a value was found (and its
color), or the value/color of the current minimum/maximum node.

### Display

- **8 (structured view)** prints the tree sideways (like a horizontal tree
  diagram), one node per line, indented by depth, in the form:

  ```
  (12)--(RED->0)
  ```

  meaning "node with value 12, color RED, at depth 0 from the node you're
  looking at it relative to". Right subtree is printed above, left below,
  so reading top-to-bottom traces the tree the way you'd draw it rotated
  90°.

- **9 (in-order view)** prints a flat left-to-right listing:
  `value(COLOR) value(COLOR) ...` — a quick way to confirm the values are
  in sorted order (a red-black tree is still a BST) and to eyeball where
  the reds and blacks fall.

### Validate

Option **10** walks the whole tree and checks all five red-black
properties, printing a violation message and its location if anything is
wrong, or a confirmation if the tree is valid. This was also used during
development to stress-test the implementation against thousands of random
insert/delete sequences.

## Notes on the implementation

- Uses a single shared `NIL` **sentinel node** (always colored BLACK)
  instead of `NULL` pointers for leaves, which is the standard approach
  (from Cormen/Leiserson/Rivest/Stein's *Introduction to Algorithms*) and
  keeps the fix-up code free of special-case null checks.
- Deletion uses the in-order **successor** to replace a node with two
  children, then runs the double-black fix-up (`delete_fixup`) on the node
  that took the deleted node's place.
- Duplicate values are rejected on insert (an existing value insert simply
  reports "already exists" rather than corrupting the tree).
- Memory is properly freed on deletion of individual nodes and on program
  exit (`rb_destroy`), so there are no leaks in normal use.

## Prerequisites to understand the code

- Pointers and structures in C
- Dynamic memory allocation (`malloc`/`free`)
- Basic binary search tree concepts (in-order traversal, min/max via
  leftmost/rightmost descent, BST deletion cases)
