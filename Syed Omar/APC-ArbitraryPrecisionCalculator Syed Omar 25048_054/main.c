#include "apc.h"
#include <stdio.h>

int validate_cli_arguments(int argc, char *argv[], char *operator);

int main(int argc, char *argv[])
{
    Dlist *head1 = NULL, *tail1 = NULL;
    Dlist *head2 = NULL, *tail2 = NULL;
    Dlist *headR = NULL, *tailR = NULL;
    char operator;
    char sign1 = '+', sign2 = '+';
    char result_sign = '+';

    if (validate_cli_arguments(argc, argv, &operator) == FAILURE)
        return FAILURE;

    if (argv[1][0] == '-') sign1 = '-';
    if (argv[3][0] == '-') sign2 = '-';

    digit_to_list(&head1, &tail1, &head2, &tail2, argv);

    switch (operator)
    {
        case '+':
        {
            if (sign1 == sign2)
            {
                addition(&head1, &tail1, &head2, &tail2, &headR, &tailR);
                result_sign = sign1;
            }
            else
            {
                int cmp = compare_list(&head1, &head2);
                if (cmp > 0)
                {
                    subtraction(&head1, &tail1, &head2, &tail2, &headR, &tailR);
                    result_sign = sign1;
                }
                else if (cmp < 0)
                {
                    subtraction(&head2, &tail2, &head1, &tail1, &headR, &tailR);
                    result_sign = sign2;
                }
                else
                    insert_at_last(&headR, &tailR, 0);
            }
            if (result_sign == '-' && !(headR->data == 0 && headR->next == NULL))
                printf("-");
            print_result(headR);
            break;
        }

        case '-':
        {
            sign2 = (sign2 == '+') ? '-' : '+';
            if (sign1 == sign2)
            {
                addition(&head1, &tail1, &head2, &tail2, &headR, &tailR);
                result_sign = sign1;
            }
            else
            {
                int cmp = compare_list(&head1, &head2);
                if (cmp > 0)
                {
                    subtraction(&head1, &tail1, &head2, &tail2, &headR, &tailR);
                    result_sign = sign1;
                }
                else if (cmp < 0)
                {
                    subtraction(&head2, &tail2, &head1, &tail1, &headR, &tailR);
                    result_sign = sign2;
                }
                else
                    insert_at_last(&headR, &tailR, 0);
            }
            if (result_sign == '-' && !(headR->data == 0 && headR->next == NULL))
                printf("-");
            print_result(headR);
            break;
        }

        case '*':
        case 'x':
        {
            multiplication(&head1, &tail1, &head2, &tail2, &headR, &tailR);
            if (sign1 != sign2) result_sign = '-';
            if (result_sign == '-' && !(headR->data == 0 && headR->next == NULL))
                printf("-");
            print_result(headR);
            break;
        }

        case '/':
        {
            if (division(&head1, &tail1, &head2, &tail2, &headR, &tailR) == FAILURE)
            {
                printf("Division failed (division by zero)\n");
                delete_list(&head1, &tail1);
                delete_list(&head2, &tail2);
                delete_list(&headR, &tailR);
                return FAILURE;
            }
            if (sign1 != sign2) result_sign = '-';
            if (result_sign == '-' && !(headR->data == 0 && headR->next == NULL))
                printf("-");
            print_result(headR);
            break;
        }

        default:
            printf("Invalid operator!\n");
    }

    delete_list(&head1, &tail1);
    delete_list(&head2, &tail2);
    delete_list(&headR, &tailR);
    return SUCCESS;
}

int validate_cli_arguments(int argc, char *argv[], char *operator)
{
    if (argc != 4)
    {
        printf("Usage: %s <operand1> <operator> <operand2>\n", argv[0]);
        return FAILURE;
    }
    *operator = argv[2][0];
    if (*operator != '+' && *operator != '-' &&
        *operator != '*' && *operator != 'x' && *operator != '/')
    {
        printf("Invalid operator!\n");
        return FAILURE;
    }
    return SUCCESS;
}