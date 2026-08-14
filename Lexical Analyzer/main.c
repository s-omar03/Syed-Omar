#include <stdio.h>
#include "analyser.h"

int main(int argc, char *argv[])
{
    if (argc == 2)
    {
        if (validate_arg(argv) == e_success)
        {
            printf("Arguments validated successfully\n");
            do_analyse(argv[1]);
        }
        else
        {
            printf("_______________HELP_______________\n");
            printf("For analysing provide argument:\n");
            printf("./a.out <filename.c>\n");
            return e_failure;
        }
    }
    else
    {
        printf("Invalid number of arguments!\n");
        printf("Usage: ./a.out <filename.c>\n");
        return e_failure;
    }
    return e_success;
}
