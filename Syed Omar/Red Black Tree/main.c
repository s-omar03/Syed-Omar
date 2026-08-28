/*
 * 
 *  main.c — Interactive CLI for the Red-Black Tree
 * 
 *  This file contains only the menu/interaction logic. All
 *  tree logic (insert, delete, search, rotations, fix-ups,
 *  validation, printing) lives in rbtree.c / rbtree.h.
 * 
 */

#include <stdio.h>
#include "rbtree.h"

static void print_menu(void) {
    printf("\n============ RED-BLACK TREE MENU ============\n");
    printf(" 1. Insert a value\n");
    printf(" 2. Delete a value\n");
    printf(" 3. Search for a value\n");
    printf(" 4. Find Minimum\n");
    printf(" 5. Delete Minimum\n");
    printf(" 6. Find Maximum\n");
    printf(" 7. Delete Maximum\n");
    printf(" 8. Display tree (structured view)\n");
    printf(" 9. Display tree (in-order view)\n");
    printf("10. Validate red-black properties\n");
    printf(" 0. Exit\n");
    printf("===============================================\n");
    printf("Enter your choice: ");
}

static int read_int(const char *prompt) {
    int value;
    printf("%s", prompt);
    while (scanf("%d", &value) != 1) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) {}
        printf("Invalid input. %s", prompt);
    }
    return value;
}

int main(void) {
    RBTree *tree = rb_create();
    int choice;

    printf("Welcome to the Red-Black Tree CLI!\n");

    do {
        print_menu();
        choice = read_int("");

        switch (choice) {
            case 1: {
                int v = read_int("Value to insert: ");
                printf("\nBefore insertion:\n");
                rb_print(tree);
                if (rb_insert(tree, v)) {
                    printf("\nAfter inserting %d (with rebalancing):\n", v);
                    rb_print(tree);
                } else {
                    printf("Value %d already exists in the tree.\n", v);
                }
                break;
            }
            case 2: {
                int v = read_int("Value to delete: ");
                printf("\nBefore deletion:\n");
                rb_print(tree);
                if (rb_delete_value(tree, v)) {
                    printf("\nAfter deleting %d (with rebalancing):\n", v);
                    rb_print(tree);
                } else {
                    printf("Value %d not found in the tree.\n", v);
                }
                break;
            }
            case 3: {
                int v = read_int("Value to search for: ");
                Node *found = rb_search(tree, tree->root, v);
                if (found)
                    printf("Found %d, color = %s\n", found->value, rb_color_name(found->color));
                else
                    printf("%d not found in the tree.\n", v);
                break;
            }
            case 4: {
                Node *m = rb_find_min(tree);
                if (m != tree->NIL)
                    printf("Minimum value = %d, color = %s\n", m->value, rb_color_name(m->color));
                else
                    printf("Tree is empty.\n");
                break;
            }
            case 5: {
                printf("\nBefore deleting minimum:\n");
                rb_print(tree);
                if (rb_delete_min(tree)) {
                    printf("\nAfter deleting minimum (with rebalancing):\n");
                    rb_print(tree);
                } else {
                    printf("Tree is empty.\n");
                }
                break;
            }
            case 6: {
                Node *m = rb_find_max(tree);
                if (m != tree->NIL)
                    printf("Maximum value = %d, color = %s\n", m->value, rb_color_name(m->color));
                else
                    printf("Tree is empty.\n");
                break;
            }
            case 7: {
                printf("\nBefore deleting maximum:\n");
                rb_print(tree);
                if (rb_delete_max(tree)) {
                    printf("\nAfter deleting maximum (with rebalancing):\n");
                    rb_print(tree);
                } else {
                    printf("Tree is empty.\n");
                }
                break;
            }
            case 8:
                rb_print(tree);
                break;
            case 9:
                rb_print_inorder(tree);
                break;
            case 10:
                if (rb_validate(tree))
                    printf("Tree satisfies all red-black properties. \xE2\x9C\x94\n");
                break;
            case 0:
                printf("Goodbye!\n");
                break;
            default:
                printf("Invalid choice. Please try again.\n");
        }
    } while (choice != 0);

    rb_destroy(tree);
    return 0;
}
