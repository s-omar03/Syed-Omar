#ifndef DISPLAY_SEARCH_H
#define DISPLAY_SEARCH_H

void displayText(void);
void showCursorPosition(void);

int findText(const char *needle);                        /* prints matches, returns count */
int replaceText(const char *oldStr, const char *newStr);   /* returns number of replacements */

#endif /* DISPLAY_SEARCH_H */
