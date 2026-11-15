#include "apc.h"

int addition(Dlist **head1, Dlist **tail1, Dlist **head2, Dlist **tail2, Dlist **hr, Dlist **tr)
{
    (void)head1; (void)head2;
    Dlist *a = *tail1, *b = *tail2;
    int carry = 0, sum;

    while (a || b || carry) {
        sum = (a ? a->data : 0) + (b ? b->data : 0) + carry;
        insert_at_first(hr, tr, sum % 10);
        carry = sum / 10;
        if (a) a = a->prev;
        if (b) b = b->prev;
    }
    return SUCCESS;
}