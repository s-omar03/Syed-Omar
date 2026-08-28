#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "text_editor.h"
#include "text_ops.h"
#include "curser_navigations.h"
#include "file_ops.h"
#include "display_search.h"
#include "redo_undo.h"

#define DEFAULT_LINE_CAPACITY 32

/* ------------------------------------------------------------------ */
/* global state                                                        */
/* ------------------------------------------------------------------ */
TextEditor editor;
DynamicArrayStack undoStack;
DynamicArrayStack redoStack;

/* ------------------------------------------------------------------ */
/* node / editor lifecycle                                             */
/* ------------------------------------------------------------------ */
Node *createNode(const char *text) {
    Node *node = malloc(sizeof(Node));
    if (!node) {
        fprintf(stderr, "Fatal: out of memory allocating line\n");
        exit(EXIT_FAILURE);
    }
    int len = (int)strlen(text);
    int cap = DEFAULT_LINE_CAPACITY;
    while (cap < len + 1) cap *= 2;

    node->text = malloc(cap);
    memcpy(node->text, text, len + 1);
    node->length = len;
    node->capacity = cap;
    node->prev = NULL;
    node->next = NULL;
    return node;
}

void freeNode(Node *node) {
    if (!node) return;
    free(node->text);
    free(node);
}

void initEditor(void) {
    editor.head = editor.tail = editor.cursor = NULL;
    editor.cursorLine = 0;
    editor.cursorPos = 0;
    editor.totalLines = 0;
    editor.clipboard = NULL;
    editor.filename = NULL;
    stackInit(&undoStack);
    stackInit(&redoStack);
}

void freeEditor(void) {
    Node *n = editor.head;
    while (n) {
        Node *next = n->next;
        freeNode(n);
        n = next;
    }
    free(editor.clipboard);
    free(editor.filename);
    stackFree(&undoStack);
    stackFree(&redoStack);
}

/* ------------------------------------------------------------------ */
/* small string helpers                                                */
/* ------------------------------------------------------------------ */
static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

/* Strips a single pair of surrounding double quotes, if present. */
static char *stripQuotes(char *s) {
    int len = (int)strlen(s);
    if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
        s[len - 1] = '\0';
        return s + 1;
    }
    return s;
}

/* Splits `rest` into up to `maxArgs` whitespace-separated tokens,
 * honoring "double quoted phrases" as a single token. Mutates `rest`.
 * Returns the number of tokens found. */
static int tokenize(char *rest, char **args, int maxArgs) {
    int count = 0;
    char *p = rest;
    while (*p && count < maxArgs) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (*p == '"') {
            p++;
            args[count++] = p;
            char *end = strchr(p, '"');
            if (end) {
                *end = '\0';
                p = end + 1;
            } else {
                p += strlen(p);
            }
        } else {
            args[count++] = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (*p) { *p = '\0'; p++; }
        }
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* UI                                                                    */
/* ------------------------------------------------------------------ */
static void printBanner(void) {
    printf("\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\n");
    printf("                    TEXT EDITOR v1.0\n");
    printf("\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\n\n");
}

void printMenu(void) {
    printf("Text Editor Commands:\n");
    printf("  insert <text>              Insert text at the cursor position\n");
    printf("  newline                    Split the current line at the cursor (Enter)\n");
    printf("  join                       Merge the current line with the next line\n");
    printf("  delete <num_chars>         Delete characters before the cursor\n");
    printf("  deleteline <num>           Delete an entire line by number\n");
    printf("  copy <num_chars>           Copy characters before the cursor\n");
    printf("  cut <num_chars>            Cut characters before the cursor\n");
    printf("  paste                      Paste the clipboard at the cursor\n");
    printf("  undo / redo                Undo or redo the last operation\n");
    printf("  up / down / left / right   Move the cursor\n");
    printf("  home / end                 Jump to the start / end of the line\n");
    printf("  top / bottom               Jump to the start / end of the document\n");
    printf("  print                      Print the current state of the text editor\n");
    printf("  pos                        Show the current cursor position\n");
    printf("  find \"text\"                Search for text\n");
    printf("  replace \"old\" \"new\"        Replace text\n");
    printf("  open <file>                Open a file\n");
    printf("  save [file]                Save the current document\n");
    printf("  close                      Close the current file\n");
    printf("  help                       Show this menu\n");
    printf("  exit                       Exit the text editor\n");
}

/* ------------------------------------------------------------------ */
/* command dispatch                                                     */
/* ------------------------------------------------------------------ */
int handleCommand(char *line) {
    char *trimmed = trim(line);
    if (*trimmed == '\0') {
        return 1;
    }

    char *rest = trimmed;
    char *cmd = trimmed;
    while (*rest && !isspace((unsigned char)*rest)) rest++;
    if (*rest) { *rest = '\0'; rest++; }
    rest = trim(rest);

    if (strcmp(cmd, "insert") == 0) {
        if (*rest == '\0') {
            printf("\xE2\x9D\x8C Error: Usage: insert <text>\n");
            return 1;
        }
        int lineBefore = editor.cursorLine, posBefore = editor.cursorPos;
        if (editor.head == NULL) { lineBefore = 1; posBefore = 0; }
        if (insertText(rest)) {
            recordAction(ACTION_INSERT, rest, lineBefore, posBefore);
            printf("\xE2\x9C\x85 Text inserted successfully.\n");
        }
    } else if (strcmp(cmd, "newline") == 0) {
        if (!editor.cursor) {
            printf("\xE2\x9D\x8C Error: Document is empty; nothing to split.\n");
            return 1;
        }
        int lineBefore = editor.cursorLine, posBefore = editor.cursorPos;
        splitLineAtCursor();
        recordAction(ACTION_NEWLINE, "", lineBefore, posBefore);
        printf("\xE2\x9C\x85 New line created.\n");
    } else if (strcmp(cmd, "join") == 0) {
        if (!editor.cursor || !editor.cursor->next) {
            printf("\xE2\x9D\x8C Error: No next line to join with.\n");
            return 1;
        }
        /* the join always merges at the END of the current line, regardless
         * of where the cursor happens to sit within it, so the undo split
         * point must be the line's length, not editor.cursorPos */
        int lineBefore = editor.cursorLine, posBefore = editor.cursor->length;
        joinWithNextLine();
        recordAction(ACTION_JOIN, "", lineBefore, posBefore);
        printf("\xE2\x9C\x85 Lines joined successfully.\n");
    } else if (strcmp(cmd, "delete") == 0) {
        if (!editor.cursor) {
            printf("\xE2\x9D\x8C Error: Document is empty; nothing to delete.\n");
            return 1;
        }
        int n = atoi(rest);
        int available = editor.cursorPos;
        if (n <= 0) {
            printf("\xE2\x9D\x8C Error: Usage: delete <positive number of characters>\n");
        } else if (n > available) {
            printf("\xE2\x9D\x8C Error: Cannot delete %d characters. Only %d characters available.\n", n, available);
        } else {
            char *removed = strndup(editor.cursor->text + editor.cursorPos - n, n);
            int lineBefore = editor.cursorLine;
            deleteCharacters(n);
            recordAction(ACTION_DELETE, removed, lineBefore, editor.cursorPos);
            free(removed);
            printf("\xE2\x9C\x85 Deleted %d characters successfully.\n", n);
        }
    } else if (strcmp(cmd, "deleteline") == 0) {
        int n = atoi(rest);
        if (deleteLine(n)) {
            printf("\xE2\x9C\x85 Line %d deleted successfully.\n", n);
        } else {
            printf("\xE2\x9D\x8C Error: Line %d does not exist.\n", n);
        }
    } else if (strcmp(cmd, "copy") == 0) {
        int n = atoi(rest);
        int available = editor.cursor ? editor.cursorPos : 0;
        if (n <= 0 || n > available) {
            printf("\xE2\x9D\x8C Error: Cannot copy %d characters. Only %d characters available.\n", n, available);
        } else {
            copyText(n);
            printf("\xE2\x9C\x85 Copied %d characters to clipboard.\n", n);
        }
    } else if (strcmp(cmd, "cut") == 0) {
        int n = atoi(rest);
        int available = editor.cursor ? editor.cursorPos : 0;
        if (n <= 0 || n > available) {
            printf("\xE2\x9D\x8C Error: Cannot cut %d characters. Only %d characters available.\n", n, available);
        } else {
            int lineBefore = editor.cursorLine;
            cutText(n);
            recordAction(ACTION_DELETE, editor.clipboard, lineBefore, editor.cursorPos);
            printf("\xE2\x9C\x85 Cut %d characters to clipboard.\n", n);
        }
    } else if (strcmp(cmd, "paste") == 0) {
        if (!editor.clipboard || strlen(editor.clipboard) == 0) {
            printf("\xE2\x9D\x8C Error: Clipboard is empty.\n");
        } else {
            int lineBefore = editor.cursorLine, posBefore = editor.cursorPos;
            if (editor.head == NULL) { lineBefore = 1; posBefore = 0; }
            pasteText();
            recordAction(ACTION_INSERT, editor.clipboard, lineBefore, posBefore);
            printf("\xE2\x9C\x85 Pasted clipboard contents.\n");
        }
    } else if (strcmp(cmd, "undo") == 0) {
        performUndo();
    } else if (strcmp(cmd, "redo") == 0) {
        performRedo();
    } else if (strcmp(cmd, "up") == 0) {
        if (!moveCursorUp()) printf("\xE2\x9D\x8C Error: Already at the first line.\n");
    } else if (strcmp(cmd, "down") == 0) {
        if (!moveCursorDown()) printf("\xE2\x9D\x8C Error: Already at the last line.\n");
    } else if (strcmp(cmd, "left") == 0) {
        if (!moveCursorLeft()) printf("\xE2\x9D\x8C Error: Already at the start of the document.\n");
    } else if (strcmp(cmd, "right") == 0) {
        if (!moveCursorRight()) printf("\xE2\x9D\x8C Error: Already at the end of the document.\n");
    } else if (strcmp(cmd, "home") == 0) {
        jumpToStartOfLine();
    } else if (strcmp(cmd, "end") == 0) {
        jumpToEndOfLine();
    } else if (strcmp(cmd, "top") == 0) {
        jumpToStartOfFile();
    } else if (strcmp(cmd, "bottom") == 0) {
        jumpToEndOfFile();
    } else if (strcmp(cmd, "print") == 0) {
        displayText();
    } else if (strcmp(cmd, "pos") == 0) {
        showCursorPosition();
    } else if (strcmp(cmd, "find") == 0) {
        char *needle = stripQuotes(rest);
        findText(needle);
    } else if (strcmp(cmd, "replace") == 0) {
        char *args[2];
        int n = tokenize(rest, args, 2);
        if (n < 2) {
            printf("\xE2\x9D\x8C Error: Usage: replace \"old\" \"new\"\n");
        } else {
            replaceText(args[0], args[1]);
        }
    } else if (strcmp(cmd, "open") == 0) {
        if (*rest == '\0') {
            printf("\xE2\x9D\x8C Error: Usage: open <filename>\n");
        } else {
            openFile(rest);
        }
    } else if (strcmp(cmd, "save") == 0) {
        saveFile(*rest ? rest : NULL);
    } else if (strcmp(cmd, "close") == 0) {
        closeFile();
    } else if (strcmp(cmd, "help") == 0) {
        printMenu();
    } else if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
        printf("\xF0\x9F\x91\x8B Goodbye! Thank you for using Text Editor v1.0\n");
        return 0;
    } else {
        printf("\xE2\x9D\x8C Error: Unknown command '%s'.\n", cmd);
        printf("Type 'help' for available commands.\n");
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* entry point                                                          */
/* ------------------------------------------------------------------ */
int main(void) {
    initEditor();
    printBanner();
    printMenu();

    char *lineBuf = NULL;
    size_t bufSize = 0;
    int running = 1;

    while (running) {
        printf("\nEnter command: ");
        fflush(stdout);
        ssize_t r = getline(&lineBuf, &bufSize, stdin);
        if (r == -1) {
            printf("\n\xF0\x9F\x91\x8B Goodbye! Thank you for using Text Editor v1.0\n");
            break;
        }
        running = handleCommand(lineBuf);
    }

    free(lineBuf);
    freeEditor();
    return 0;
}
