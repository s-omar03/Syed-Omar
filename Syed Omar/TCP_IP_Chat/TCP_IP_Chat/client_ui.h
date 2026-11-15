/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : client_ui.h
 *           Descriptions   : Terminal helpers of the chat client: a line reader that works
 *                            with poll(), password echo control, safe printing of text that
 *                            came from the network, and prompt tracking
 *           Objective      : Keep keyboard and screen handling out of the client state machine
 *           Usage          : #include "client_ui.h"
 *           Output         : Text on stdout
 *
###############################################################################################*/

#ifndef CLIENT_UI_H
#define CLIENT_UI_H

#include <stddef.h>

#define UI_LINE_MAX          1024                /* longest input line accepted     */
#define UI_LINE_BUFFER_SIZE  (UI_LINE_MAX + 1)   /* line plus its newline           */

/* line_reader_next() results */
typedef enum
{
    LINE_NONE,              /* no complete line buffered yet             */
    LINE_READY,             /* a line was returned                       */
    LINE_TOO_LONG,          /* a line longer than UI_LINE_MAX was thrown away */
    LINE_EOF                /* end of input and nothing left             */
} LineStatus;

/* Input buffer that splits whatever read() returns into lines */
typedef struct
{
    char buf[UI_LINE_BUFFER_SIZE];
    size_t len;             /* bytes buffered                            */
    int eof;                /* read() returned 0 or failed               */
    int discarding;         /* skipping the rest of an over-long line    */
} LineReader;

void line_reader_init(LineReader *reader);
int line_reader_fill(LineReader *reader, int fd);
LineStatus line_reader_next(LineReader *reader, char *line, size_t size);

int ui_echo_off(int fd);
void ui_echo_restore(void);

void ui_print_safe(const char *text);
void ui_show_prompt(const char *prompt);
int ui_prompt_visible(void);
void ui_prompt_consumed(void);

#endif
