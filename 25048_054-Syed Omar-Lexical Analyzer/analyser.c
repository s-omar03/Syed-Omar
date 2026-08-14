#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "analyser.h"

/* Keyword table (defined once) */
const char keywords[KEYWORD_COUNT][20] = {
    "int","float","char","double","if","else","for","while","do","return",
    "void","break","continue","switch","case","default","long","short",
    "unsigned","signed","static","struct","typedef","const","goto","enum",
    "union","extern","auto","volatile","register","sizeof"
};

/* validate argument (.c extension) */
status validate_arg(char* argv[])
{
    if (!argv || !argv[1]) return e_failure;
    const char *ext = strrchr(argv[1], '.');
    if (ext && strcmp(ext, ".c") == 0) return e_success;
    printf("Unsupported file format (Only accept .c files)\n");
    return e_failure;
}

/* open file and set analyser->fptr */
status open_file(analyser *a, const char *filename)
{
    if (!a || !filename) return e_failure;
    a->fptr = fopen(filename, "r");
    if (a->fptr == NULL) {
        printf("Can't open that file: %s\n", filename);
        return e_failure;
    }
    return e_success;
}

/* check space (space, tab, carriage return) but not newline here */
status is_space(char ch)
{
    if (ch == ' ' || ch == '\t' || ch == '\r') return e_success;
    return e_failure;
}

/* Preprocessor: read until newline and print token */
void preprocessor(analyser *a)
{
    char buffer[MAX_LEN];
    int k = 0;
    int ch;

    if (!a || !a->fptr) return;

    buffer[k++] = '#';
    while ((ch = fgetc(a->fptr)) != EOF && ch != '\n' && k < MAX_LEN - 1) {
        buffer[k++] = (char)ch;
    }
    buffer[k] = '\0';
    a->line++; /* consume newline */

    printf("%-18s -> %-15s\n", "PREPROCESSOR", buffer);
}

/* Comment handler: supports single-line (//) and multi-line comments */
void comment_handler(analyser *a, int next)
{
    int ch;
    if (!a || !a->fptr) return;

    if (next == '/') {
        /* single-line comment: skip until newline */
        while ((ch = fgetc(a->fptr)) != EOF && ch != '\n');
        if (ch == '\n') a->line++;
        /* for demonstration, we don't print comments */
    } else if (next == '*') {
        /* multi-line comment */
        a->command = 1;
        while ((ch = fgetc(a->fptr)) != EOF) {
            if (ch == '\n') a->line++;
            if (ch == '*') {
                int p = fgetc(a->fptr);
                if (p == '/') {
                    a->command = 0;
                    break;
                } else {
                    if (p != EOF) ungetc(p, a->fptr);
                }
            }
        }
    }
}

/* string literal handler (handles simple escapes) */
void stringliteral(analyser *a)
{
    char buffer[MAX_LEN];
    int k = 0;
    int ch;
    if (!a || !a->fptr) return;

    buffer[k++] = '"';
    while ((ch = fgetc(a->fptr)) != EOF) {
        buffer[k++] = (char)ch;
        if (ch == '"') {
            a->strings = 0;
            break;
        }
        if (ch == '\\') { /* escape sequence: include next char too */
            int nxt = fgetc(a->fptr);
            if (nxt == EOF) break;
            if (k < MAX_LEN - 1) buffer[k++] = (char)nxt;
            if (nxt == '\n') a->line++;
        }
        if (ch == '\n') { /* rarely allowed but count lines inside */
            a->line++;
        }
        if (k >= MAX_LEN - 1) break;
    }
    buffer[k] = '\0';

    printf("%-18s -> %-15s\n", "STRING LITERAL", buffer);
}

/* identifier or keyword extraction */
void iskeyword_OR_identifier(char ch, analyser *a)
{
    char buffer[MAX_LEN];
    int k = 0;
    int c;
    if (!a || !a->fptr) return;

    buffer[k++] = ch;
    while ((c = fgetc(a->fptr)) != EOF && (isalnum(c) || c == '_')) {
        if (k < MAX_LEN - 1) buffer[k++] = (char)c;
    }
    buffer[k] = '\0';
    if (c != EOF) ungetc(c, a->fptr);

    /* check keyword */
    for (int i = 0; i < KEYWORD_COUNT; ++i) {
        if (strcmp(buffer, keywords[i]) == 0) {
            printf("%-18s -> %-15s\n", "KEYWORD", buffer);
            return;
        }
    }
    printf("%-18s -> %-15s\n", "IDENTIFIER", buffer);
}

/* number literal parser (integers and simple floats with exponent) */
void number_literal(char ch, analyser *a)
{
    char buffer[MAX_LEN];
    int k = 0;
    int c;
    if (!a || !a->fptr) return;

    buffer[k++] = ch;
    int seen_dot = 0;
    int seen_exp = 0;

    while ((c = fgetc(a->fptr)) != EOF) {
        if (isdigit(c)) {
            if (k < MAX_LEN - 1) buffer[k++] = (char)c;
            continue;
        }
        if ((c == '.' && !seen_dot && !seen_exp) ) {
            seen_dot = 1;
            if (k < MAX_LEN - 1) buffer[k++] = '.';
            continue;
        }
        if ((c == 'e' || c == 'E') && !seen_exp) {
            seen_exp = 1;
            if (k < MAX_LEN - 1) buffer[k++] = (char)c;
            /* optional + or - after e */
            int p = fgetc(a->fptr);
            if (p == '+' || p == '-') {
                if (k < MAX_LEN - 1) buffer[k++] = (char)p;
            } else {
                if (p != EOF) ungetc(p, a->fptr);
            }
            continue;
        }
        break;
    }
    buffer[k] = '\0';
    if (c != EOF) ungetc(c, a->fptr);

    printf("%-18s -> %-15s\n", "NUMBER", buffer);
}

/* delimiter handling and counters */
void delimiter(char ch, analyser *a)
{
    if (!a) return;
    switch (ch) {
        case '(':
            a->parantheses++;
            printf("%-18s -> %-15c\n", "OPEN PARENTHESIS", ch);
            break;
        case ')':
            a->parantheses--;
            printf("%-18s -> %-15c\n", "CLOSED PARENTHESIS", ch);
            break;
        case '{':
            a->braces++;
            printf("%-18s -> %-15c\n", "OPEN BRACE", ch);
            break;
        case '}':
            a->braces--;
            printf("%-18s -> %-15c\n", "CLOSED BRACE", ch);
            break;
        case ';':
            printf("%-18s -> %-15c\n", "SEMICOLON", ch);
            break;
        case ',':
            printf("%-18s -> %-15c\n", "COMMA", ch);
            break;
        case '[':
            printf("%-18s -> %-15c\n", "OPEN BRACKET", ch);
            break;
        case ']':
            printf("%-18s -> %-15c\n", "CLOSED BRACKET", ch);
            break;
        default:
            printf("%-18s -> %-15c\n", "DELIM", ch);
            break;
    }
}

/* Operator: support common multi-char operators minimally */
void Operator(char ch, analyser *a)
{
    int next;
    if (!a || !a->fptr) return;

    /* read next char to check multi-char ops */
    next = fgetc(a->fptr);
    if (next == EOF) {
        printf("%-18s -> %-15c\n", "OPERATOR", ch);
        return;
    }

    /* Check common multi-char operators */
    if (ch == '=' && next == '=') { printf("%-18s -> %-15s\n", "OPERATOR", "=="); return; }
    if (ch == '!' && next == '=') { printf("%-18s -> %-15s\n", "OPERATOR", "!="); return; }
    if (ch == '<' && next == '=') { printf("%-18s -> %-15s\n", "OPERATOR", "<="); return; }
    if (ch == '>' && next == '=') { printf("%-18s -> %-15s\n", "OPERATOR", ">="); return; }
    if (ch == '+' && next == '+') { printf("%-18s -> %-15s\n", "OPERATOR", "++"); return; }
    if (ch == '-' && next == '-') { printf("%-18s -> %-15s\n", "OPERATOR", "--"); return; }
    if (ch == '+' && next == '=') { printf("%-18s -> %-15s\n", "OPERATOR", "+="); return; }
    if (ch == '-' && next == '=') { printf("%-18s -> %-15s\n", "OPERATOR", "-="); return; }
    if (ch == '*' && next == '=') { printf("%-18s -> %-15s\n", "OPERATOR", "*="); return; }
    if (ch == '/' && next == '=') { printf("%-18s -> %-15s\n", "OPERATOR", "/="); return; }
    if (ch == '&' && next == '&') { printf("%-18s -> %-15s\n", "OPERATOR", "&&"); return; }
    if (ch == '|' && next == '|') { printf("%-18s -> %-15s\n", "OPERATOR", "||"); return; }
    if (ch == '-' && next == '>') { printf("%-18s -> %-15s\n", "OPERATOR", "->"); return; }

    /* not multi-char: push back next and print single char */
    ungetc(next, a->fptr);
    printf("%-18s -> %-15c\n", "OPERATOR", ch);
}

/* detect comment or operator starting with '/' */
void is_command(char ch, analyser *a)
{
    if (!a || !a->fptr) return;
    if (ch == '/') {
        int next = fgetc(a->fptr);
        if (next == EOF) {
            printf("%-18s -> %-15c\n", "OPERATOR", ch);
            return;
        }
        if (next == '/' || next == '*') {
            /* comment: handle and return (comment consumes input) */
            comment_handler(a, next);
            return;
        } else {
            /* maybe '/=' or just '/' operator */
            ungetc(next, a->fptr);
            Operator(ch, a);
            return;
        }
    }
}

/* main analysis routine */
status do_analyse(char* filename)
{
    analyser a;
    a.line = 1;
    a.braces = 0;
    a.parantheses = 0;
    a.strings = 0;
    a.command = 0;

    if (open_file(&a, filename) != e_success) return e_failure;

    printf("_______________LEXICAL ANALYSER_______________\n");
    printf("Tokens:\n");

    int ch;
    while ((ch = fgetc(a.fptr)) != EOF) {

        /* newline handling */
        if (ch == '\n') {
            a.line++;
            continue;
        }

        /* skip spaces and tabs */
        if (is_space(ch) == e_success) continue;

        /* preprocessor */
        if (ch == '#') {
            preprocessor(&a);
            continue;
        }

        /* identifier or keyword */
        if (isalpha(ch) || ch == '_') {
            iskeyword_OR_identifier((char)ch, &a);
            continue;
        }

        /* number */
        if (isdigit(ch)) {
            number_literal((char)ch, &a);
            continue;
        }

        /* delimiters */
        if (strchr(";,(){}[]", ch)) {
            delimiter((char)ch, &a);
            continue;
        }

        /* comment or operator starting with '/' */
        if (ch == '/') {
            int next = fgetc(a.fptr);
            if (next == EOF) {
                Operator((char)ch, &a);
                break;
            }
            if (next == '/' || next == '*') {
                comment_handler(&a, next);
                continue;
            } else {
                ungetc(next, a.fptr);
                Operator((char)ch, &a);
                continue;
            }
        }

        /* string literal */
        if (ch == '"') {
            a.strings = 1;
            stringliteral(&a);
            continue;
        }

        /* character literal (simple handling) */
        if (ch == '\'') {
            char buffer[8] = {0};
            int k = 0;
            int c = fgetc(a.fptr);
            if (c == '\\') { /* escape */
                int nxt = fgetc(a.fptr);
                buffer[k++] = '\\'; if (k < 7) buffer[k++] = (char)nxt;
            } else {
                if (c != EOF) buffer[k++] = (char)c;
            }
            /* expect closing quote */
            int closing = fgetc(a.fptr);
            (void)closing; /* ignore validation here */
            buffer[k] = '\0';
            printf("%-18s -> '%s'\n", "CHAR LITERAL", buffer);
            continue;
        }

        /* operators (fallback) */
        if (strchr("+-*%<>=!&|^~?:", ch)) {
            Operator((char)ch, &a);
            continue;
        }

        /* any other char: print as unknown/operator */
        printf("%-18s -> %-15c\n", "UNKNOWN", ch);
    }

    fclose(a.fptr);

    /* observations */
    printf("\n----------------------------------------\n");
    printf("OBSERVATIONS:\n");
    if (a.parantheses != 0) printf("POSSIBLE ERROR: Unmatched parentheses detected!\n");
    if (a.braces != 0) printf("POSSIBLE ERROR: Unmatched braces detected!\n");
    if (a.strings) printf("POSSIBLE ERROR: Unterminated string literal!\n");
    if (a.command) printf("POSSIBLE ERROR: Unterminated /* comment */!\n");
    if (!a.parantheses && !a.braces && !a.strings && !a.command)
        printf("No lexical errors detected.\n");
    printf("----------------------------------------\n");

    return e_success;
}