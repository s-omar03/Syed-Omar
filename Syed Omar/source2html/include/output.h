#ifndef OUTPUT_H
#define OUTPUT_H
#include "parser.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

void set_output_file(FILE* fptr); // tells output.c which file to write
void handle_token(Token t);

#endif
