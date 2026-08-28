#include <string.h>
#include "text_editor.h"
#include "curser_navigations.h"

int moveCursorUp(void) {
    if (!editor.cursor || !editor.cursor->prev) return 0;
    editor.cursor = editor.cursor->prev;
    editor.cursorLine -= 1;
    if (editor.cursorPos > editor.cursor->length) {
        editor.cursorPos = editor.cursor->length;
    }
    return 1;
}

int moveCursorDown(void) {
    if (!editor.cursor || !editor.cursor->next) return 0;
    editor.cursor = editor.cursor->next;
    editor.cursorLine += 1;
    if (editor.cursorPos > editor.cursor->length) {
        editor.cursorPos = editor.cursor->length;
    }
    return 1;
}

int moveCursorLeft(void) {
    if (!editor.cursor) return 0;
    if (editor.cursorPos > 0) {
        editor.cursorPos -= 1;
        return 1;
    }
    if (editor.cursor->prev) {
        editor.cursor = editor.cursor->prev;
        editor.cursorLine -= 1;
        editor.cursorPos = editor.cursor->length;
        return 1;
    }
    return 0;
}

int moveCursorRight(void) {
    if (!editor.cursor) return 0;
    if (editor.cursorPos < editor.cursor->length) {
        editor.cursorPos += 1;
        return 1;
    }
    if (editor.cursor->next) {
        editor.cursor = editor.cursor->next;
        editor.cursorLine += 1;
        editor.cursorPos = 0;
        return 1;
    }
    return 0;
}

void jumpToStartOfFile(void) {
    if (!editor.head) return;
    editor.cursor = editor.head;
    editor.cursorLine = 1;
    editor.cursorPos = 0;
}

void jumpToEndOfFile(void) {
    if (!editor.tail) return;
    editor.cursor = editor.tail;
    editor.cursorLine = editor.totalLines;
    editor.cursorPos = editor.tail->length;
}

void jumpToStartOfLine(void) {
    editor.cursorPos = 0;
}

void jumpToEndOfLine(void) {
    if (editor.cursor) {
        editor.cursorPos = editor.cursor->length;
    }
}
