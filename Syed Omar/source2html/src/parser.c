#include "../include/parser.h"

static void emit(const char* source, int start, int end, TokenType type, void (*on_token)(Token) ) {

    if ( end <= start ) return; // nothing to emit so returning

    Token t;
    t.type = type;
    t.start = source + start;
    t.length = end - start;
    on_token(t);
}

void parse(const char *source, int length, void (*on_token)(Token)) {

    ParserState state = STATE_NORMAL;
    int i = 0; // current position in source
    int token_start = 0; // where the current token started

    while (i < length) {
        char ch = source[i];

        switch (state) {

            case STATE_NORMAL:
                if ( ch == '/' ) {
                    emit(source,token_start,i,TOKEN_TEXT,on_token);
                    token_start = i;
                    state = STATE_SAW_SLASH;
                    i+=1;
                }else if (ch == '#'){
                    emit(source,token_start,i,TOKEN_TEXT,on_token); 
                    token_start = i;
                    state = STATE_IN_PREPROCESSOR;
                    i+=1;
                } else if ( ch == '"' ) {
                    emit(source, token_start, i, TOKEN_TEXT, on_token);
                    token_start = i;
                    state = STATE_IN_STRING;
                    i+=1;
                } else if ( isdigit(ch)) {
                    emit(source, token_start, i, TOKEN_TEXT, on_token);
                    token_start = i;
                    state = STATE_IN_NUMBER;
                    i+=1;
                } else if ( ch == '\'') {
                    emit(source, token_start, i, TOKEN_TEXT, on_token);
                    token_start = i;
                    state = STATE_IN_CHAR_LITERAL;
                    i+=1;
                }else {
                    i+=1;
                }
                break;

            case STATE_SAW_SLASH:
                if ( ch == '/') {
                    state = STATE_IN_LINE_COMMENT;
                    i+=1;
                } else if ( ch == '*') {
                    state = STATE_IN_BLOCK_COMMENT;
                }else {
                    emit(source,token_start,i,TOKEN_TEXT,on_token);
                    token_start = i;
                    state = STATE_NORMAL;
                }
                break;

            case STATE_IN_LINE_COMMENT:
                if ( ch == '\n' ) {
                    emit(source,token_start,i+1,TOKEN_COMMENT_LINE,on_token);
                    token_start = i + 1;
                    state = STATE_NORMAL;
                    i+=1;
                } else {
                    i+=1;
                }
                break;

            case STATE_IN_BLOCK_COMMENT:
                if ( ch == '*') {
                    state = STATE_SAW_STAR;
                    i+=1;
                } else {
                    i+=1;
                }
                break;

            case STATE_SAW_STAR: 
                if ( ch == '/' ) {
                    emit(source, token_start, i+1,TOKEN_COMMENT_BLOCK, on_token);
                    token_start = i+1;
                    state = STATE_NORMAL;
                    i+=1;
                } else if ( ch == '*') {
                    state = STATE_SAW_STAR;
                    i+=1;
                } else {
                    state = STATE_IN_BLOCK_COMMENT;
                    i+=1;
                }
                break;

            case STATE_IN_PREPROCESSOR:
                if ( ch  == '\n') {
                    emit(source,token_start,i+1,TOKEN_PREPROCESSOR, on_token);
                    token_start = i+1;
                    state = STATE_NORMAL;
                    i+=1;
                } else {
                    i+=1;
                }
                break;

            case STATE_IN_STRING:
                /* FIX: bounds-check the escape lookahead. The original
                 * `i += 2` on backslash could skip past `length` when a
                 * string was left unterminated at EOF (e.g. a stray
                 * backslash as the very last byte of the file), which
                 * would read/emit past the end of the buffer. Clamping
                 * keeps the loop condition (i < length) safe.
                 */
                if ( ch == '\\') {
                    i += (i + 2 <= length) ? 2 : (length - i);
                } else if ( ch == '"')  {
                    emit(source, token_start, i+1,TOKEN_STRING, on_token);
                    token_start = i+1;
                    state = STATE_NORMAL;
                    i+=1;
                } else {
                    i+=1;
                }
                break;

            case STATE_IN_NUMBER:
                /* FIX: originally only isdigit(ch) continued the number,
                 * so floats (3.14), hex literals (0xFF), and suffixes
                 * (10L, 3.14f, 100u) were split into multiple tokens.
                 * A syntax highlighter doesn't need to validate that the
                 * literal is well-formed C -- it just needs to capture the
                 * whole run of characters that make up one numeric token.
                 * isalnum() + '.' covers digits, hex digits, x/X, and
                 * suffix letters (L, U, f, etc.) in one pass.
                 */
                if ( isalnum((unsigned char)ch) || ch == '.') {
                    i+=1;
                } else {
                    emit(source, token_start, i,TOKEN_NUMBER, on_token);
                    token_start = i;
                    state = STATE_NORMAL;
                }
                break;

            case  STATE_IN_CHAR_LITERAL: 
                /* Same EOF-safety fix as STATE_IN_STRING above. */
                if ( ch == '\\') {
                    i += (i + 2 <= length) ? 2 : (length - i);
                } else if ( ch == '\'') {
                    emit(source, token_start,i+1,TOKEN_CHAR_LITERAL,on_token);
                    token_start = i+1;
                    state = STATE_NORMAL;
                    i+=1;
                } else {
                    i+=1;
                }
                break;

            default:
                i+=1;
                break;
        }
    }

    /* Flush whatever token is still open when we hit EOF, using the
     * state we ended in rather than always TOKEN_TEXT, so an unterminated
     * comment/string/number at end-of-file is still highlighted instead
     * of silently reverting to plain text. */
    TokenType final_type;
    switch (state) {
        case STATE_IN_LINE_COMMENT:  final_type = TOKEN_COMMENT_LINE;  break;
        case STATE_IN_BLOCK_COMMENT:
        case STATE_SAW_STAR:         final_type = TOKEN_COMMENT_BLOCK; break;
        case STATE_IN_PREPROCESSOR:  final_type = TOKEN_PREPROCESSOR;  break;
        case STATE_IN_STRING:        final_type = TOKEN_STRING;        break;
        case STATE_IN_NUMBER:        final_type = TOKEN_NUMBER;        break;
        case STATE_IN_CHAR_LITERAL:  final_type = TOKEN_CHAR_LITERAL;  break;
        default:                     final_type = TOKEN_TEXT;         break;
    }
    emit(source,token_start,i,final_type,on_token);
}
