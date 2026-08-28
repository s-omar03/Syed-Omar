#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text_editor.h"
#include "display_search.h"
#include "text_ops.h"

#define BOX_MIN_WIDTH 59

/* utf-8 box drawing characters */
#define BOX_TL "\xE2\x94\x8C"
#define BOX_TR "\xE2\x94\x90"
#define BOX_BL "\xE2\x94\x94"
#define BOX_BR "\xE2\x94\x98"
#define BOX_H  "\xE2\x94\x80"
#define BOX_V  "\xE2\x94\x82"

static int utf8DisplayLen(const char *s) {
    /* good enough approximation for ASCII-heavy editor content */
    int len = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if ((*p & 0xC0) != 0x80) len++;
    }
    return len;
}

static void printBoxLine(const char *content, int width) {
    int pad = width - utf8DisplayLen(content);
    if (pad < 0) pad = 0;
    printf("%s %s%*s%s\n", BOX_V, content, pad, "", BOX_V);
}

void displayText(void) {
    if (!editor.head) {
        printf("(document is empty)\n");
        return;
    }

    int width = BOX_MIN_WIDTH;
    char lineLabel[512];
    for (Node *n = editor.head; n; n = n->next) {
        int lineNo = 1;
        Node *counter = editor.head;
        while (counter != n) { counter = counter->next; lineNo++; }
        snprintf(lineLabel, sizeof(lineLabel), "Line %d: %s", lineNo, n->text);
        int len = utf8DisplayLen(lineLabel);
        if (len > width) width = len;
    }
    char cursorLabel[64];
    snprintf(cursorLabel, sizeof(cursorLabel), "Cursor position: Line %d, Column %d",
              editor.cursorLine, editor.cursorPos);
    int cLen = utf8DisplayLen(cursorLabel);
    if (cLen > width) width = cLen;
    width += 2;

    printf("%s", BOX_TL);
    for (int i = 0; i < width + 2; i++) printf("%s", BOX_H);
    printf("%s\n", BOX_TR);

    int lineNo = 1;
    for (Node *n = editor.head; n; n = n->next, lineNo++) {
        snprintf(lineLabel, sizeof(lineLabel), "Line %d: %s", lineNo, n->text);
        printBoxLine(lineLabel, width);
    }
    printBoxLine(cursorLabel, width);

    printf("%s", BOX_BL);
    for (int i = 0; i < width + 2; i++) printf("%s", BOX_H);
    printf("%s\n", BOX_BR);
}

void showCursorPosition(void) {
    printf("Cursor position: Line %d, Column %d\n", editor.cursorLine, editor.cursorPos);
}

int findText(const char *needle) {
    if (!needle || strlen(needle) == 0) {
        printf("\xE2\x9D\x8C Error: search text cannot be empty.\n");
        return 0;
    }
    int needleLen = (int)strlen(needle);
    int matches = 0;
    int lineNo = 1;
    for (Node *n = editor.head; n; n = n->next, lineNo++) {
        const char *pos = n->text;
        while ((pos = strstr(pos, needle)) != NULL) {
            int column = (int)(pos - n->text);
            printf("\xF0\x9F\x94\x8D Found \"%s\" at Line %d, Column %d\n", needle, lineNo, column);
            matches++;
            pos += needleLen;
        }
    }
    if (matches == 0) {
        printf("\xF0\x9F\x94\x8D \"%s\" was not found.\n", needle);
    }
    printf("\xE2\x9C\x85 Search completed. %d match%s found.\n", matches, matches == 1 ? "" : "es");
    return matches;
}

int replaceText(const char *oldStr, const char *newStr) {
    if (!oldStr || strlen(oldStr) == 0) {
        printf("\xE2\x9D\x8C Error: search text cannot be empty.\n");
        return 0;
    }
    printf("\xF0\x9F\x94\x84 Replacing \"%s\" with \"%s\"...\n", oldStr, newStr);

    int oldLen = (int)strlen(oldStr);
    int newLen = (int)strlen(newStr);
    int totalReplacements = 0;

    for (Node *n = editor.head; n; n = n->next) {
        char *result = malloc(n->capacity > 0 ? n->capacity : 32);
        int resultCap = n->capacity > 0 ? n->capacity : 32;
        int resultLen = 0;
        const char *src = n->text;

        while (*src) {
            if (strncmp(src, oldStr, oldLen) == 0 && oldLen > 0) {
                if (resultLen + newLen + 1 > resultCap) {
                    resultCap = (resultLen + newLen + 1) * 2;
                    result = realloc(result, resultCap);
                }
                memcpy(result + resultLen, newStr, newLen);
                resultLen += newLen;
                src += oldLen;
                totalReplacements++;
            } else {
                if (resultLen + 1 + 1 > resultCap) {
                    resultCap *= 2;
                    result = realloc(result, resultCap);
                }
                result[resultLen++] = *src++;
            }
        }
        result[resultLen] = '\0';

        free(n->text);
        n->text = result;
        n->length = resultLen;
        n->capacity = resultCap;

        if (n == editor.cursor && editor.cursorPos > n->length) {
            editor.cursorPos = n->length;
        }
    }

    printf("\xE2\x9C\x85 Replaced %d occurrence%s.\n", totalReplacements, totalReplacements == 1 ? "" : "s");
    return totalReplacements;
}
