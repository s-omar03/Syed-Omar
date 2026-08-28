#ifndef CURSER_NAVIGATIONS_H
#define CURSER_NAVIGATIONS_H

int moveCursorUp(void);
int moveCursorDown(void);
int moveCursorLeft(void);
int moveCursorRight(void);

void jumpToStartOfFile(void);
void jumpToEndOfFile(void);
void jumpToStartOfLine(void);
void jumpToEndOfLine(void);

#endif /* CURSER_NAVIGATIONS_H */
