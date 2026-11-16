#ifndef TYPE_H
#define TYPE_H

#include <stdio.h>

/* Unsigned 32-bit integer used for frame and tag sizes */
typedef unsigned int uint;

/* Return codes used throughout the program */
typedef enum
{
    success,
    failure
} Status;

/* Operations recognised on the command line */
typedef enum
{
    view,
    edit,
    help,
    unsupported
} Operation;

#endif
