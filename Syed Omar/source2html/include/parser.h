#ifndef PARSER_H
#define PARSER_H
#include <ctype.h>

typedef enum {
    TOKEN_TEXT,
    TOKEN_COMMENT_LINE,
    TOKEN_COMMENT_BLOCK,
    TOKEN_STRING,
    TOKEN_PREPROCESSOR,
    TOKEN_KEYWORD,
    TOKEN_NUMBER,
    TOKEN_CHAR_LITERAL
} TokenType;

typedef struct {
    TokenType type;
    const char* start; // points to start of source buffer
    int length;
} Token;

typedef enum {
    STATE_NORMAL,
    STATE_SAW_STAR,
    STATE_SAW_SLASH,
    STATE_IN_LINE_COMMENT,
    STATE_IN_BLOCK_COMMENT,
    STATE_IN_STRING,
    STATE_IN_PREPROCESSOR,
    STATE_IN_NUMBER,
    STATE_IN_CHAR_LITERAL
} ParserState;

void parse(const char* source, int length, void (*on_token)(Token));

#endif
