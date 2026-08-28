#ifndef TEXT_EDITOR_H
#define TEXT_EDITOR_H

#include <stddef.h>

/* ---------- Doubly linked list node: one line of text ---------- */
typedef struct Node {
    char *text;          /* heap-allocated, null-terminated line content   */
    int length;           /* current length of text (excluding '\0')       */
    int capacity;          /* allocated capacity of text buffer              */
    struct Node *prev;
    struct Node *next;
} Node;

/* ---------- Main editor state ---------- */
typedef struct {
    Node *head;        /* first line            */
    Node *tail;         /* last line              */
    Node *cursor;        /* line the cursor is on  */
    int cursorLine;       /* 1-indexed line number  */
    int cursorPos;         /* 0-indexed column       */
    int totalLines;
    char *clipboard;      /* last copy/cut buffer   */
    char *filename;        /* currently open file, or NULL */
} TextEditor;

/* ---------- Undo/redo action ---------- */
typedef enum { ACTION_INSERT, ACTION_DELETE, ACTION_NEWLINE, ACTION_JOIN } ActionType;

typedef struct {
    ActionType type;
    char *text;        /* text inserted or deleted */
    int cursorLine;      /* where the action occurred */
    int cursorPos;
} Action;

/* ---------- Dynamic array based stack of Actions ---------- */
typedef struct {
    Action *actions;
    int size;
    int capacity;
} DynamicArrayStack;

/* ---------- global editor + undo/redo stacks (defined in text_editor.c) ---------- */
extern TextEditor editor;
extern DynamicArrayStack undoStack;
extern DynamicArrayStack redoStack;

/* helper shared across modules */
Node *createNode(const char *text);
void freeNode(Node *node);
void initEditor(void);
void freeEditor(void);
void printMenu(void);
int handleCommand(char *line);   /* returns 0 to request program exit, 1 to continue */

#endif /* TEXT_EDITOR_H */
