#include "apc.h"
#include <stdio.h>

/*
 * Long division, one digit of the dividend at a time (like on paper).
 *
 * For each digit: bring it down into the running remainder, then subtract
 * the divisor from the remainder as many times as it fits (at most 9 times).
 * That count is the next digit of the quotient.
 *
 * The work is proportional to (number of digits) x 9 subtractions, so a
 * 1000-digit number divides instantly. The old version subtracted the divisor
 * once per unit of the quotient, which took forever for large quotients.
 */
int division(Dlist **head1, Dlist **tail1,
             Dlist **head2, Dlist **tail2,
             Dlist **headR, Dlist **tailR)
{
    Dlist *remH = NULL, *remT = NULL;      /* running remainder */
    Dlist *tempH, *tempT;
    Dlist *digit;
    (void)tail1;
    int count;

    /* Division by zero */
    if (*head2 == NULL ||
       ((*head2)->data == 0 && (*head2)->next == NULL))
        return FAILURE;

    for (digit = *head1; digit != NULL; digit = digit->next)
    {
        /* remainder = remainder * 10 + this digit */
        insert_at_last(&remH, &remT, digit->data);
        remove_leading_zeros(&remH, &remT);

        count = 0;
        while (compare_list(&remH, head2) >= 0)
        {
            tempH = tempT = NULL;
            subtraction(&remH, &remT, head2, tail2, &tempH, &tempT);
            delete_list(&remH, &remT);
            remH = tempH;
            remT = tempT;
            remove_leading_zeros(&remH, &remT);
            count++;
        }

        insert_at_last(headR, tailR, count);
    }

    delete_list(&remH, &remT);

    if (*headR == NULL)
        insert_at_last(headR, tailR, 0);
    remove_leading_zeros(headR, tailR);

    return SUCCESS;
}
