#ifndef TEXT_OPS_H
#define TEXT_OPS_H

#include "text_editor.h"
int insertText(const char *text);                 /* returns 1 on success */
int deleteCharacters(int numChars);                 /* backspace-style, bounded to current line */
int splitLineAtCursor(void);                          /* "Enter" - create a new line */
int joinWithNextLine(void);                             /* merge current line with the next one */
int deleteLine(int lineNumber);                          /* remove an entire line by number */

int copyText(int numChars);                                /* copy N chars before cursor into clipboard */
int cutText(int numChars);                                   /* copy + delete */
int pasteText(void);                                          /* insert clipboard contents at cursor */

/* low level helper used by several modules */
void growLineCapacity(Node *node, int minCapacity);

#endif /* TEXT_OPS_H */
