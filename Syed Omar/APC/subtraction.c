#include "apc.h"

int subtraction(Dlist **head1, Dlist **tail1, Dlist **head2, Dlist **tail2, Dlist **hr, Dlist **tr)
{
    (void)head1; (void)head2;
    Dlist *a = *tail1, *b = *tail2;
    int borrow = 0, d1, d2;

    while (a) {
        d1 = a->data - borrow;
        d2 = b ? b->data : 0;
        if (d1 < d2) { d1 += 10; borrow = 1; }
        else borrow = 0;
        insert_at_first(hr, tr, d1 - d2);
        a = a->prev;
        if (b) b = b->prev;
    }
    return SUCCESS;
}