# Text Editor v1.0

A console-based text editor written in C, built around a doubly linked list
of lines and a dynamic-array-backed undo/redo stack.

## Build

```bash
make          # builds ./text_editor
make run      # builds and runs it
make clean    # removes build artifacts
```

Requires GCC and a POSIX environment (uses `getline`/`strndup`). Compiles
cleanly with `-std=c99 -Wall -Wextra` and has been checked with
AddressSanitizer + UndefinedBehaviorSanitizer.

## Architecture

| File | Responsibility |
|---|---|
| `text_editor.h/.c` | Core structures, `main()`, menu, command dispatch |
| `text_ops.h/.c` | insert / delete / newline / join / copy / cut / paste / deleteline |
| `curser_navigations.h/.c` | up / down / left / right / home / end / top / bottom |
| `file_ops.h/.c` | open / save / close |
| `display_search.h/.c` | print / find / replace |
| `redo_undo.h/.c` | Dynamic-array undo/redo stacks |

Text is stored as a doubly linked list of `Node`s, one per line, each with
its own dynamically growable character buffer. Every mutating command
(insert, delete, newline, join) is captured as an `Action` and pushed onto
`undoStack`; running `undo` pops it, reverses the exact mutation, and pushes
it onto `redoStack` (and vice-versa for `redo`). A fresh edit clears the
redo stack, as in most real editors.

## Commands

```
insert <text>              Insert text at the cursor position
newline                    Split the current line at the cursor (Enter)
join                       Merge the current line with the next line
delete <num_chars>         Delete characters before the cursor (backspace-style)
deleteline <num>           Delete an entire line by number
copy <num_chars>           Copy characters before the cursor
cut <num_chars>            Cut characters before the cursor
paste                      Paste the clipboard at the cursor
undo / redo                Undo or redo the last operation
up / down / left / right   Move the cursor
home / end                 Jump to the start / end of the line
top / bottom               Jump to the start / end of the document
print                      Print the current state of the text editor
pos                        Show the current cursor position
find "text"                Search for text (quotes optional if no spaces)
replace "old" "new"        Replace all occurrences
open <file>                Open a file
save [file]                Save (defaults to the currently open filename)
close                      Close the current file
help                       Show the command list
exit                       Exit the editor
```

## Example session

```
$ ./text_editor
Enter command: insert Hello World
✅ Text inserted successfully.

Enter command: print
┌─────────────────────────────────────────┐
│ Line 1: Hello World                      │
│ Cursor position: Line 1, Column 11       │
└─────────────────────────────────────────┘

Enter command: newline
✅ New line created.

Enter command: insert Programming is fun!
✅ Text inserted successfully.

Enter command: delete 5
✅ Deleted 5 characters successfully.

Enter command: undo
✅ Undo operation successful.

Enter command: exit
👋 Goodbye! Thank you for using Text Editor v1.0
```

## Notes on this implementation

- `delete`, `copy`, and `cut` are backspace-style: they operate on the
  `N` characters immediately **before** the cursor, and are bounded to the
  current line (an editor-wide "delete across line boundaries" is done
  explicitly via `join`, which is itself undoable).
- `replace` is a bulk operation across the whole document; each individual
  substitution is not pushed onto the undo stack, but the overall action
  isn't destructive to anything `undo` already knows about, so undoing
  earlier edits still works fine afterward.
- Unicode box-drawing characters are used for `print`; the box width
  adapts to the longest line.
