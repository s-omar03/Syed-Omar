#ifndef ANALYSER_H
#define ANALYSER_H

#include <stdio.h>

#define MAX_LEN 128
#define KEYWORD_COUNT 32

typedef enum {
    e_failure = 1,
    e_success = 0
} status;

typedef struct {
    FILE *fptr;
    int line;
    int braces;
    int parantheses;
    int strings;
    int command;   /* in_comment flag */
} analyser;

/* function prototypes (your style) */
status validate_arg(char *argv[]);
status open_file(analyser *a, const char *filename);
status do_analyse(char *filename);
status is_space(char ch);

void is_command(char ch, analyser *a);
void preprocessor(analyser *a);
void iskeyword_OR_identifier(char ch, analyser *a);
void number_literal(char ch, analyser *a);
void Operator(char ch, analyser *a);
void delimiter(char ch, analyser *a);
void comment_handler(analyser *a, int next);
void stringliteral(analyser *a);

#endif
