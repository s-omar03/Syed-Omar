#ifndef TYPE_H
#define TYPE_H

typedef unsigned int uint;

typedef enum
{
    success,
    failure
}Status;

typedef enum
{
    view,
    edit,
    help,
    unsupported
}Operation;

#endif