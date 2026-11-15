#include "apc.h"
#include <stdio.h>

int division(Dlist **head1, Dlist **tail1,
             Dlist **head2, Dlist **tail2,
             Dlist **headR, Dlist **tailR)
{
    Dlist *tempH = NULL, *tempT = NULL;
    int count;

    if (*head2 == NULL ||
       ((*head2)->data == 0 && (*head2)->next == NULL))
        return FAILURE;

    if (compare_list(head1, head2) < 0)
    {
        insert_at_last(headR, tailR, 0);
        return SUCCESS;
    }

    while (compare_list(head1, head2) >= 0)
    {
        count = 0;
        tempH = tempT = NULL;

        while (compare_list(head1, head2) >= 0)
        {
            subtraction(head1, tail1, head2, tail2, &tempH, &tempT);
            delete_list(head1, tail1);
            *head1 = tempH;
            *tail1 = tempT;
            remove_leading_zeros(head1, tail1);
            count++;
            tempH = tempT = NULL;
        }

        char buffer[50];
        sprintf(buffer, "%d", count);
        for (int i = 0; buffer[i] != '\0'; i++)
            insert_at_last(headR, tailR, buffer[i] - '0');
    }

    if (*headR == NULL)
        insert_at_last(headR, tailR, 0);

    return SUCCESS;
}