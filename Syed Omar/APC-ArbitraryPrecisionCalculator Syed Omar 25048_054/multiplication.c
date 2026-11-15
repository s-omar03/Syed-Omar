#include "apc.h"

int multiplication(Dlist **head1, Dlist **tail1, Dlist **head2, Dlist **tail2, Dlist **hr, Dlist **tr)
{
    (void)head1; (void)head2;
    Dlist *a, *b = *tail2;
    Dlist *rh = NULL, *rt = NULL, *th, *tt;
    int carry, prod, zeros = 0;

    while (b) {
        a = *tail1; carry = 0; th = tt = NULL;
        for (int i = 0; i < zeros; i++) insert_at_last(&th, &tt, 0);
        while (a) {
            prod = (a->data * b->data) + carry;
            insert_at_first(&th, &tt, prod % 10);
            carry = prod / 10;
            a = a->prev;
        }
        if (carry) insert_at_first(&th, &tt, carry);

        if (!rh) { rh = th; rt = tt; }
        else {
            addition(&rh, &rt, &th, &tt, hr, tr);
            delete_list(&rh, &rt);
            delete_list(&th, &tt);
            rh = *hr; rt = *tr;
            *hr = *tr = NULL;
        }
        b = b->prev;
        zeros++;
    }
    *hr = rh; *tr = rt;
    return SUCCESS;
}