#include "../include/output.h"

static const char* keywords_data[] = {
    "int", "char", "float", "double", "void",
    "struct", "union", "enum", "typedef",
    "const", "unsigned", "signed", "long", "short",
    "extern", "static", "auto", "register", NULL
};

static const char* keywords_control[] = {
    "if", "else", "while", "for", "do",
    "return", "break", "continue", "goto",
    "switch", "case", "default", "sizeof", NULL
};

static int is_keyword(const char** keywords, const char* word, int len) {
    for (int i = 0; keywords[i] != NULL; i++) {
        int klen = strlen(keywords[i]);
        if (klen == len && strncmp(word, keywords[i], len) == 0)
            return 1;
    }
    return 0;
}

static FILE* g_dest = NULL;

void set_output_file(FILE *fptr) {
    g_dest = fptr;
}

static void fwrite_escaped(const char* s, int length, FILE* f) {
    for ( int i = 0 ; i < length ; i++ ) {

        if ( s[i] == '<' ) fprintf(f,"&lt;");
        else if ( s[i] == '>' ) fprintf(f,"&gt;");
        else if ( s[i] == '&' ) fprintf(f,"&amp;");
        else fputc(s[i],f);
    }
}

static void write_with_keywords(const char* s, int length, FILE* f) {
    int i = 0;
    while (i < length) {
        if ( isalpha((unsigned char)s[i]) || s[i] == '_' ) {
            // start of a word

            int start = i;
            while ( i < length && (isalnum((unsigned char)s[i]) || s[i] == '_') ) {
                i++;
            }
            int word_len = i - start;
            if (is_keyword(keywords_data, s+start, word_len)) {
                fprintf(f, "<span class='keyword_data'>");
                fwrite_escaped(s+start, word_len, f);
                fprintf(f, "</span>");
            } else if (is_keyword(keywords_control, s+start, word_len)) {
                fprintf(f, "<span class='keyword_control'>");
                fwrite_escaped(s+start, word_len, f);
                fprintf(f, "</span>");
            } else {
                fwrite_escaped(s+start, word_len, f);
            }

        } else {
            // not a word character

            if ( s[i] == '<') fprintf(f, "&lt;");
            else if ( s[i] == '>') fprintf(f, "&gt;");
            else if ( s[i] == '&') fprintf(f, "&amp;");
            else fputc(s[i],f);
            i+=1;
        }
    }
}


void handle_token(Token t) {
    /* FIX: `j` used to be declared *inside* the TOKEN_COMMENT_LINE case
     * of this switch but consumed by the TOKEN_PREPROCESSOR case below
     * it. Since a switch's cases don't each get their own scope, `j`
     * was only guaranteed to be 0 if a comment-line token happened to
     * be handled earlier in the same run. Any preprocessor token
     * arriving first read garbage stack memory -- undefined behavior.
     * Declaring and initializing it once at function scope fixes that.
     */
    int j = 0;

    switch (t.type) {

        case TOKEN_COMMENT_LINE: 
            fprintf(g_dest,"<span class='comment'>");
            /* NOTE: fwrite inspite of fprintf
             * our token is not an null terminated string
             * it is just a pointer into the middle of a buffer
             * with a length. fprintf will keep reading until it hit on '\0'
             * fwrite stops exactly at t.length
             */
            fwrite_escaped(t.start,t.length,g_dest);
            fprintf(g_dest,"</span>");
            break;
        case TOKEN_PREPROCESSOR:
            while (j < t.length && t.start[j] != ' ' && t.start[j] != '\n' ) {
                j+=1;
            }
            fprintf(g_dest,"<span class='preprocessor'>");
            fwrite_escaped(t.start,j, g_dest);
            fprintf(g_dest, "</span>");

            fprintf(g_dest, "<span class='header'>");
            fwrite_escaped(t.start + j, t.length - j, g_dest);
            fprintf(g_dest, "</span>");

            break;
        case TOKEN_STRING: 
            fprintf(g_dest,"<span class='string'>");
            fwrite_escaped(t.start,t.length,g_dest);
            fprintf(g_dest,"</span>");
            break;

        case TOKEN_COMMENT_BLOCK: 
            fprintf(g_dest, "<span class='comment'>");
            fwrite_escaped(t.start, t.length, g_dest);
            fprintf(g_dest, "</span>");
            break;

        case TOKEN_NUMBER: 
            fprintf(g_dest, "<span class='number'>");
            fwrite_escaped(t.start, t.length, g_dest);
            fprintf(g_dest, "</span>");
            break;

        case TOKEN_CHAR_LITERAL: 
            fprintf(g_dest, "<span class='char'>");
            fwrite_escaped(t.start, t.length, g_dest);
            fprintf(g_dest, "</span>");
            break;

        default: 
            write_with_keywords(t.start, t.length, g_dest);
            break;
    }
}
