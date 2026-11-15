#ifndef APC_H
#define APC_H

#include <stddef.h>
#include <stdlib.h>

#define SUCCESS 0
#define FAILURE -1

typedef struct node
{
    struct node *prev;
    int data;
    struct node *next;
} Dlist;

void digit_to_list(Dlist **head1, Dlist **tail1,
                   Dlist **head2, Dlist **tail2,
                   char *argv[]);

int addition(Dlist **head1, Dlist **tail1,
             Dlist **head2, Dlist **tail2,
             Dlist **headR, Dlist **tailR);

int subtraction(Dlist **head1, Dlist **tail1,
                Dlist **head2, Dlist **tail2,
                Dlist **headR, Dlist **tailR);

int multiplication(Dlist **head1, Dlist **tail1,
                   Dlist **head2, Dlist **tail2,
                   Dlist **headR, Dlist **tailR);

int division(Dlist **head1, Dlist **tail1,
             Dlist **head2, Dlist **tail2,
             Dlist **headR, Dlist **tailR);

void print_result(Dlist *head);
int insert_at_first(Dlist **head, Dlist **tail, int data);
int insert_at_last(Dlist **head, Dlist **tail, int data);
int compare_list(Dlist **head1, Dlist **head2);
void delete_list(Dlist **head, Dlist **tail);
void remove_leading_zeros(Dlist **head, Dlist **tail);

#endif