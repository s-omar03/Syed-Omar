#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text_editor.h"
#include "file_ops.h"
#include "redo_undo.h"

/* Prints an integer with thousands separators, e.g. 1234 -> "1,234". */
static void printWithCommas(long value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", value);
    int len = (int)strlen(buf);
    for (int i = 0; i < len; i++) {
        putchar(buf[i]);
        int remaining = len - i - 1;
        if (remaining > 0 && remaining % 3 == 0) {
            putchar(',');
        }
    }
}

static void resetDocument(void) {
    Node *n = editor.head;
    while (n) {
        Node *next = n->next;
        freeNode(n);
        n = next;
    }
    editor.head = editor.tail = editor.cursor = NULL;
    editor.cursorLine = 0;
    editor.cursorPos = 0;
    editor.totalLines = 0;
    stackClear(&undoStack);
    stackClear(&redoStack);
}

int openFile(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        printf("\xE2\x9D\x8C Error: File '%s' not found.\n", filename);
        return 0;
    }

    resetDocument();

    char *lineBuf = NULL;
    size_t bufSize = 0;
    ssize_t readLen;
    long totalChars = 0;
    int lineCount = 0;

    while ((readLen = getline(&lineBuf, &bufSize, fp)) != -1) {
        if (readLen > 0 && lineBuf[readLen - 1] == '\n') {
            lineBuf[readLen - 1] = '\0';
            readLen -= 1;
        }
        Node *node = createNode(lineBuf);
        if (!editor.head) {
            editor.head = editor.tail = node;
        } else {
            node->prev = editor.tail;
            editor.tail->next = node;
            editor.tail = node;
        }
        totalChars += readLen;
        lineCount += 1;
    }
    free(lineBuf);
    fclose(fp);

    if (lineCount == 0) {
        /* empty file: start with a single empty line */
        Node *node = createNode("");
        editor.head = editor.tail = node;
        lineCount = 1;
    }

    editor.cursor = editor.head;
    editor.cursorLine = 1;
    editor.cursorPos = 0;
    editor.totalLines = lineCount;

    free(editor.filename);
    editor.filename = strdup(filename);

    printf("\xE2\x9C\x85 File '%s' opened successfully.\n", filename);
    printf("\xF0\x9F\x93\x84 Loaded %d line%s, ", lineCount, lineCount == 1 ? "" : "s");
    printWithCommas(totalChars);
    printf(" characters.\n");
    return 1;
}

int saveFile(const char *filename) {
    const char *target = filename ? filename : editor.filename;
    if (!target) {
        printf("\xE2\x9D\x8C Error: No filename specified and no file currently open.\n");
        return 0;
    }
    if (!editor.head) {
        printf("\xE2\x9D\x8C Error: Nothing to save; the document is empty.\n");
        return 0;
    }

    FILE *fp = fopen(target, "w");
    if (!fp) {
        printf("\xE2\x9D\x8C Error: Could not open '%s' for writing.\n", target);
        return 0;
    }

    long totalChars = 0;
    int lineCount = 0;
    for (Node *n = editor.head; n; n = n->next) {
        fputs(n->text, fp);
        totalChars += n->length;
        if (n->next) {
            fputc('\n', fp);
        }
        lineCount += 1;
    }
    fclose(fp);

    printf("\xE2\x9C\x85 File saved successfully as '%s'\n", target);
    printf("\xF0\x9F\x92\xBE Saved %d line%s, ", lineCount, lineCount == 1 ? "" : "s");
    printWithCommas(totalChars);
    printf(" characters.\n");
    return 1;
}

int closeFile(void) {
    resetDocument();
    free(editor.filename);
    editor.filename = NULL;
    printf("\xE2\x9C\x85 File closed successfully.\n");
    return 1;
}
