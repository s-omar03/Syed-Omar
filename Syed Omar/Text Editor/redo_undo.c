#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text_editor.h"
#include "redo_undo.h"
#include "text_ops.h"
#include "curser_navigations.h"

#define INITIAL_STACK_CAPACITY 8

void stackInit(DynamicArrayStack *stack) {
    stack->capacity = INITIAL_STACK_CAPACITY;
    stack->size = 0;
    stack->actions = malloc(sizeof(Action) * stack->capacity);
    if (!stack->actions) {
        fprintf(stderr, "Fatal: out of memory initializing stack\n");
        exit(EXIT_FAILURE);
    }
}

static void stackGrow(DynamicArrayStack *stack) {
    stack->capacity *= 2;
    Action *resized = realloc(stack->actions, sizeof(Action) * stack->capacity);
    if (!resized) {
        fprintf(stderr, "Fatal: out of memory growing stack\n");
        exit(EXIT_FAILURE);
    }
    stack->actions = resized;
}

void stackPush(DynamicArrayStack *stack, ActionType type, const char *text, int line, int pos) {
    if (stack->size == stack->capacity) {
        stackGrow(stack);
    }
    Action *a = &stack->actions[stack->size++];
    a->type = type;
    a->text = text ? strdup(text) : NULL;
    a->cursorLine = line;
    a->cursorPos = pos;
}

int stackPop(DynamicArrayStack *stack, Action *out) {
    if (stack->size == 0) {
        return 0;
    }
    Action *a = &stack->actions[--stack->size];
    *out = *a;   /* caller now owns a->text (transferred pointer, not duplicated) */
    return 1;
}

void stackClear(DynamicArrayStack *stack) {
    for (int i = 0; i < stack->size; i++) {
        free(stack->actions[i].text);
    }
    stack->size = 0;
}

void stackFree(DynamicArrayStack *stack) {
    stackClear(stack);
    free(stack->actions);
    stack->actions = NULL;
    stack->capacity = 0;
}

/* Records a completed action onto the undo stack and clears the redo
 * stack (a fresh action invalidates any previously undone future). */
void recordAction(ActionType type, const char *text, int line, int pos) {
    stackPush(&undoStack, type, text, line, pos);
    stackClear(&redoStack);
}

static Node *getLineByNumber(int n);

void performUndo(void) {
    Action a;
    if (!stackPop(&undoStack, &a)) {
        printf("\xE2\x9D\x8C Error: Nothing to undo.\n");
        return;
    }

    switch (a.type) {
        case ACTION_INSERT: {
            /* undo an insertion: remove the text we had inserted */
            Node *n = getLineByNumber(a.cursorLine);
            if (n) {
                editor.cursor = n;
                editor.cursorLine = a.cursorLine;
                editor.cursorPos = a.cursorPos + (int)strlen(a.text);
                deleteCharacters((int)strlen(a.text));
            }
            break;
        }
        case ACTION_DELETE: {
            /* undo a deletion: re-insert the removed text */
            Node *n = getLineByNumber(a.cursorLine);
            if (n) {
                editor.cursor = n;
                editor.cursorLine = a.cursorLine;
                editor.cursorPos = a.cursorPos;
                insertText(a.text);
            }
            break;
        }
        case ACTION_NEWLINE: {
            /* undo a split: join the two lines back together */
            Node *n = getLineByNumber(a.cursorLine);
            if (n) {
                editor.cursor = n;
                editor.cursorLine = a.cursorLine;
                editor.cursorPos = a.cursorPos;
                joinWithNextLine();
            }
            break;
        }
        case ACTION_JOIN: {
            /* undo a join: split the line again at the recorded position */
            Node *n = getLineByNumber(a.cursorLine);
            if (n) {
                editor.cursor = n;
                editor.cursorLine = a.cursorLine;
                editor.cursorPos = a.cursorPos;
                splitLineAtCursor();
            }
            break;
        }
    }

    /* move the action to the redo stack (without re-clearing it) */
    stackPush(&redoStack, a.type, a.text, a.cursorLine, a.cursorPos);
    free(a.text);
    printf("\xE2\x9C\x85 Undo operation successful.\n");
}

void performRedo(void) {
    Action a;
    if (!stackPop(&redoStack, &a)) {
        printf("\xE2\x9D\x8C Error: Nothing to redo.\n");
        return;
    }

    switch (a.type) {
        case ACTION_INSERT: {
            Node *n = getLineByNumber(a.cursorLine);
            if (n) {
                editor.cursor = n;
                editor.cursorLine = a.cursorLine;
                editor.cursorPos = a.cursorPos;
                insertText(a.text);
            }
            break;
        }
        case ACTION_DELETE: {
            Node *n = getLineByNumber(a.cursorLine);
            if (n) {
                editor.cursor = n;
                editor.cursorLine = a.cursorLine;
                editor.cursorPos = a.cursorPos + (int)strlen(a.text);
                deleteCharacters((int)strlen(a.text));
            }
            break;
        }
        case ACTION_NEWLINE: {
            Node *n = getLineByNumber(a.cursorLine);
            if (n) {
                editor.cursor = n;
                editor.cursorLine = a.cursorLine;
                editor.cursorPos = a.cursorPos;
                splitLineAtCursor();
            }
            break;
        }
        case ACTION_JOIN: {
            Node *n = getLineByNumber(a.cursorLine);
            if (n) {
                editor.cursor = n;
                editor.cursorLine = a.cursorLine;
                editor.cursorPos = a.cursorPos;
                joinWithNextLine();
            }
            break;
        }
    }

    stackPush(&undoStack, a.type, a.text, a.cursorLine, a.cursorPos);
    free(a.text);
    printf("\xE2\x9C\x85 Redo operation successful.\n");
}

static Node *getLineByNumber(int n) {
    Node *cur = editor.head;
    int i = 1;
    while (cur && i < n) {
        cur = cur->next;
        i++;
    }
    return cur;
}
