#include "apc.h"
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>

int insert_at_last(Dlist **head, Dlist **tail, int data)
{
    Dlist *newnode = (Dlist *)malloc(sizeof(Dlist));
    if (newnode == NULL)
        return FAILURE;
    newnode->data = data;
    newnode->prev = NULL;
    newnode->next = NULL;
    if (*head == NULL)
    {
        *head = *tail = newnode;
    }
    else
    {
        (*tail)->next = newnode;
        newnode->prev = *tail;
        *tail = newnode;
    }
    return SUCCESS;
}

int insert_at_first(Dlist **head, Dlist **tail, int data)
{
    Dlist *new = (Dlist *)malloc(sizeof(Dlist));
    if (new == NULL)
        return FAILURE;
    new->data = data;
    new->prev = NULL;
    new->next = NULL;
    if (*head == NULL)
    {
        *head = *tail = new;
        return SUCCESS;
    }
    new->next = *head;
    (*head)->prev = new;
    *head = new;
    return SUCCESS;
}

void digit_to_list(Dlist **head1, Dlist **tail1,
                   Dlist **head2, Dlist **tail2,
                   char *argv[])
{
    char *op1 = argv[1];
    char *op2 = argv[3];
    int i;

    i = (op1[0] == '-') ? 1 : 0;
    while (op1[i] == '0')
        i++;
    if (op1[i] == '\0')
    {
        insert_at_last(head1, tail1, 0);
    }
    else
    {
        for (; op1[i] != '\0'; i++)
        {
            if (!isdigit(op1[i]))
            {
                printf("Invalid character in operand1\n");
                return;
            }
            insert_at_last(head1, tail1, op1[i] - '0');
        }
    }

    i = (op2[0] == '-') ? 1 : 0;
    while (op2[i] == '0')
        i++;
    if (op2[i] == '\0')
    {
        insert_at_last(head2, tail2, 0);
    }
    else
    {
        for (; op2[i] != '\0'; i++)
        {
            if (!isdigit(op2[i]))
            {
                printf("Invalid character in operand2\n");
                return;
            }
            insert_at_last(head2, tail2, op2[i] - '0');
        }
    }
}

void print_result(Dlist *head)
{
    if (head == NULL)
    {
        printf("List is empty (0)\n");
        return;
    }
    while (head->data == 0 && head->next != NULL)
        head = head->next;
    while (head != NULL)
    {
        printf("%d", head->data);
        head = head->next;
    }
    printf("\n");
}

int compare_list(Dlist **head1, Dlist **head2)
{
    int count1 = 0, count2 = 0;
    Dlist *t1 = *head1;
    Dlist *t2 = *head2;

    while (t1) { count1++; t1 = t1->next; }
    while (t2) { count2++; t2 = t2->next; }

    if (count1 > count2) return 1;
    if (count1 < count2) return -1;

    t1 = *head1;
    t2 = *head2;
    while (t1 && t2)
    {
        if (t1->data > t2->data) return 1;
        else if (t1->data < t2->data) return -1;
        t1 = t1->next;
        t2 = t2->next;
    }
    return 0;
}

void delete_list(Dlist **head, Dlist **tail)
{
    Dlist *temp;
    while (*head != NULL)
    {
        temp = *head;
        *head = (*head)->next;
        free(temp);
    }
    *tail = NULL;
}

void remove_leading_zeros(Dlist **head, Dlist **tail)
{
    while (*head && (*head)->data == 0 && (*head)->next)
    {
        Dlist *temp = *head;
        *head = (*head)->next;
        (*head)->prev = NULL;
        free(temp);
    }
    if (*head == NULL)
        *tail = NULL;
}