#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text_editor.h"
#include "text_ops.h"

#define DEFAULT_LINE_CAPACITY 32

/* Ensure a node's text buffer can hold at least minCapacity bytes
 * (including the terminating null byte), doubling as needed. */
void growLineCapacity(Node *node, int minCapacity) {
    if (node->capacity >= minCapacity) {
        return;
    }
    int newCapacity = node->capacity > 0 ? node->capacity : DEFAULT_LINE_CAPACITY;
    while (newCapacity < minCapacity) {
        newCapacity *= 2;
    }
    char *resized = realloc(node->text, newCapacity);
    if (!resized) {
        fprintf(stderr, "Fatal: out of memory growing line buffer\n");
        exit(EXIT_FAILURE);
    }
    node->text = resized;
    node->capacity = newCapacity;
}

/* Insert `text` into the current line at the current cursor position.
 * Creates the very first line if the document is currently empty. */
int insertText(const char *text) {
    if (!text) return 0;
    int addLen = (int)strlen(text);
    if (addLen == 0) return 0;

    if (editor.head == NULL) {
        Node *node = createNode("");
        editor.head = editor.tail = editor.cursor = node;
        editor.cursorLine = 1;
        editor.cursorPos = 0;
        editor.totalLines = 1;
    }

    Node *n = editor.cursor;
    growLineCapacity(n, n->length + addLen + 1);

    /* shift the tail of the line right to make room, then splice text in */
    memmove(n->text + editor.cursorPos + addLen,
            n->text + editor.cursorPos,
            n->length - editor.cursorPos + 1); /* +1 copies the null terminator */
    memcpy(n->text + editor.cursorPos, text, addLen);

    n->length += addLen;
    editor.cursorPos += addLen;
    return 1;
}

/* Backspace-style delete: removes `numChars` immediately before the
 * cursor within the CURRENT line only. Caller must ensure
 * numChars <= editor.cursorPos before calling. */
int deleteCharacters(int numChars) {
    if (!editor.cursor || numChars <= 0 || numChars > editor.cursorPos) {
        return 0;
    }
    Node *n = editor.cursor;
    int start = editor.cursorPos - numChars;

    memmove(n->text + start,
            n->text + editor.cursorPos,
            n->length - editor.cursorPos + 1); /* +1 moves the null terminator too */

    n->length -= numChars;
    editor.cursorPos = start;
    return 1;
}

/* Splits the current line into two at the cursor position ("Enter"). */
int splitLineAtCursor(void) {
    if (!editor.cursor) return 0;
    Node *n = editor.cursor;

    Node *newNode = createNode(n->text + editor.cursorPos);

    /* truncate the original line at the cursor */
    n->text[editor.cursorPos] = '\0';
    n->length = editor.cursorPos;

    /* splice newNode in right after n */
    newNode->prev = n;
    newNode->next = n->next;
    if (n->next) {
        n->next->prev = newNode;
    } else {
        editor.tail = newNode;
    }
    n->next = newNode;

    editor.cursor = newNode;
    editor.cursorLine += 1;
    editor.cursorPos = 0;
    editor.totalLines += 1;
    return 1;
}

/* Merges the current line with the NEXT line, removing the line break
 * between them. Cursor position/line are left unchanged. */
int joinWithNextLine(void) {
    if (!editor.cursor || !editor.cursor->next) return 0;
    Node *n = editor.cursor;
    Node *next = n->next;

    growLineCapacity(n, n->length + next->length + 1);
    memcpy(n->text + n->length, next->text, next->length + 1); /* copies null too */
    n->length += next->length;

    n->next = next->next;
    if (next->next) {
        next->next->prev = n;
    } else {
        editor.tail = n;
    }
    freeNode(next);
    editor.totalLines -= 1;
    return 1;
}

/* Removes an entire line (1-indexed) from the document. */
int deleteLine(int lineNumber) {
    if (lineNumber < 1 || lineNumber > editor.totalLines) return 0;

    Node *n = editor.head;
    for (int i = 1; i < lineNumber && n; i++) {
        n = n->next;
    }
    if (!n) return 0;

    if (n->prev) n->prev->next = n->next; else editor.head = n->next;
    if (n->next) n->next->prev = n->prev; else editor.tail = n->prev;

    int wasCursor = (editor.cursor == n);
    freeNode(n);
    editor.totalLines -= 1;

    if (editor.totalLines == 0) {
        editor.head = editor.tail = editor.cursor = NULL;
        editor.cursorLine = 0;
        editor.cursorPos = 0;
    } else if (wasCursor) {
        if (editor.head && lineNumber > editor.totalLines) {
            editor.cursor = editor.tail;
            editor.cursorLine = editor.totalLines;
        } else {
            Node *replacement = editor.head;
            for (int i = 1; i < lineNumber && replacement; i++) replacement = replacement->next;
            editor.cursor = replacement;
            editor.cursorLine = lineNumber;
        }
        editor.cursorPos = 0;
    } else if (editor.cursorLine > lineNumber) {
        editor.cursorLine -= 1;
    }
    return 1;
}

/* Copies `numChars` immediately before the cursor into the clipboard,
 * without modifying the document. */
int copyText(int numChars) {
    if (!editor.cursor || numChars <= 0 || numChars > editor.cursorPos) {
        return 0;
    }
    free(editor.clipboard);
    editor.clipboard = malloc(numChars + 1);
    memcpy(editor.clipboard, editor.cursor->text + editor.cursorPos - numChars, numChars);
    editor.clipboard[numChars] = '\0';
    return 1;
}

/* Copies `numChars` immediately before the cursor into the clipboard,
 * THEN removes them from the document. */
int cutText(int numChars) {
    if (!copyText(numChars)) {
        return 0;
    }
    return deleteCharacters(numChars);
}

/* Inserts the current clipboard contents at the cursor position. */
int pasteText(void) {
    if (!editor.clipboard || strlen(editor.clipboard) == 0) {
        return 0;
    }
    return insertText(editor.clipboard);
}
