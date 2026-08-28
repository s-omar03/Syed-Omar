/*
 * 
 *  rbtree.c — Red-Black Tree implementation
 * 
 *  A self-balancing binary search tree implementing:
 *    - Insertion
 *    - Deletion
 *    - Search
 *    - Find Minimum / Delete Minimum
 *    - Find Maximum / Delete Maximum
 *    - Visual (structured) tree printing showing each node's
 *      value and color, e.g.  (12)--(RED->0)
 *
 *  Properties maintained:
 *    1. Every node is either RED or BLACK.
 *    2. The root is BLACK.
 *    3. Every leaf (NIL) is BLACK.
 *    4. A RED node cannot have a RED child (no two reds in a row).
 *    5. Every path from a node to its descendant NIL leaves
 *       contains the same number of BLACK nodes (black-height).
 *
 *  See rbtree.h for the public API exposed to other files.
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include "rbtree.h"

/* ---------------------------------------------------------- */
/*  Construction helpers                                        */
/* ---------------------------------------------------------- */

static Node *new_node(RBTree *t, int value) {
    Node *n = (Node *)malloc(sizeof(Node));
    if (!n) {
        fprintf(stderr, "Error: out of memory\n");
        exit(EXIT_FAILURE);
    }
    n->value = value;
    n->color = RED; /* new nodes are always inserted as RED first */
    n->left = t->NIL;
    n->right = t->NIL;
    n->parent = t->NIL;
    return n;
}

RBTree *rb_create(void) {
    RBTree *t = (RBTree *)malloc(sizeof(RBTree));
    Node *nil = (Node *)malloc(sizeof(Node));
    nil->color = BLACK;
    nil->value = 0;
    nil->left = nil->right = nil->parent = nil; /* points to itself */
    t->NIL = nil;
    t->root = nil;
    return t;
}

/* ---------------------------------------------------------- */
/*  Rotations                                                   */
/* ---------------------------------------------------------- */

static void left_rotate(RBTree *t, Node *x) {
    Node *y = x->right;
    x->right = y->left;
    if (y->left != t->NIL) y->left->parent = x;
    y->parent = x->parent;
    if (x->parent == t->NIL) t->root = y;
    else if (x == x->parent->left) x->parent->left = y;
    else x->parent->right = y;
    y->left = x;
    x->parent = y;
}

static void right_rotate(RBTree *t, Node *x) {
    Node *y = x->left;
    x->left = y->right;
    if (y->right != t->NIL) y->right->parent = x;
    y->parent = x->parent;
    if (x->parent == t->NIL) t->root = y;
    else if (x == x->parent->right) x->parent->right = y;
    else x->parent->left = y;
    y->right = x;
    x->parent = y;
}

/* ---------------------------------------------------------- */
/*  Insertion                                                   */
/* ---------------------------------------------------------- */

static void insert_fixup(RBTree *t, Node *z) {
    while (z->parent->color == RED) {
        if (z->parent == z->parent->parent->left) {
            Node *uncle = z->parent->parent->right;
            if (uncle->color == RED) {
                /* Case 1: uncle is red -> recolor */
                z->parent->color = BLACK;
                uncle->color = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    /* Case 2: triangle -> rotate to line */
                    z = z->parent;
                    left_rotate(t, z);
                }
                /* Case 3: line -> rotate and recolor */
                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                right_rotate(t, z->parent->parent);
            }
        } else {
            Node *uncle = z->parent->parent->left;
            if (uncle->color == RED) {
                z->parent->color = BLACK;
                uncle->color = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    right_rotate(t, z);
                }
                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                left_rotate(t, z->parent->parent);
            }
        }
        if (z == t->root) break;
    }
    t->root->color = BLACK;
}

/* Returns 1 on success, 0 if the value already exists (duplicates rejected) */
int rb_insert(RBTree *t, int value) {
    Node *y = t->NIL;
    Node *x = t->root;

    while (x != t->NIL) {
        y = x;
        if (value == x->value) return 0; /* no duplicates */
        else if (value < x->value) x = x->left;
        else x = x->right;
    }

    Node *z = new_node(t, value);
    z->parent = y;

    if (y == t->NIL) t->root = z;
    else if (value < y->value) y->left = z;
    else y->right = z;

    insert_fixup(t, z);
    return 1;
}

/* ---------------------------------------------------------- */
/*  Search / Min / Max                                          */
/* ---------------------------------------------------------- */

Node *rb_search(RBTree *t, Node *x, int value) {
    if (x == t->NIL || value == x->value) return (x == t->NIL) ? NULL : x;
    if (value < x->value) return rb_search(t, x->left, value);
    return rb_search(t, x->right, value);
}

static Node *tree_minimum(RBTree *t, Node *x) {
    if (x == t->NIL) return t->NIL;
    while (x->left != t->NIL) x = x->left;
    return x;
}

static Node *tree_maximum(RBTree *t, Node *x) {
    if (x == t->NIL) return t->NIL;
    while (x->right != t->NIL) x = x->right;
    return x;
}

Node *rb_find_min(RBTree *t) { return tree_minimum(t, t->root); }
Node *rb_find_max(RBTree *t) { return tree_maximum(t, t->root); }

/* ---------------------------------------------------------- */
/*  Deletion                                                    */
/* ---------------------------------------------------------- */

static void transplant(RBTree *t, Node *u, Node *v) {
    if (u->parent == t->NIL) t->root = v;
    else if (u == u->parent->left) u->parent->left = v;
    else u->parent->right = v;
    v->parent = u->parent;
}

static void delete_fixup(RBTree *t, Node *x) {
    while (x != t->root && x->color == BLACK) {
        if (x == x->parent->left) {
            Node *w = x->parent->right;
            if (w->color == RED) {
                w->color = BLACK;
                x->parent->color = RED;
                left_rotate(t, x->parent);
                w = x->parent->right;
            }
            if (w->left->color == BLACK && w->right->color == BLACK) {
                w->color = RED;
                x = x->parent;
            } else {
                if (w->right->color == BLACK) {
                    w->left->color = BLACK;
                    w->color = RED;
                    right_rotate(t, w);
                    w = x->parent->right;
                }
                w->color = x->parent->color;
                x->parent->color = BLACK;
                w->right->color = BLACK;
                left_rotate(t, x->parent);
                x = t->root;
            }
        } else {
            Node *w = x->parent->left;
            if (w->color == RED) {
                w->color = BLACK;
                x->parent->color = RED;
                right_rotate(t, x->parent);
                w = x->parent->left;
            }
            if (w->right->color == BLACK && w->left->color == BLACK) {
                w->color = RED;
                x = x->parent;
            } else {
                if (w->left->color == BLACK) {
                    w->right->color = BLACK;
                    w->color = RED;
                    left_rotate(t, w);
                    w = x->parent->left;
                }
                w->color = x->parent->color;
                x->parent->color = BLACK;
                w->left->color = BLACK;
                right_rotate(t, x->parent);
                x = t->root;
            }
        }
    }
    x->color = BLACK;
}

/* Deletes the given node z from the tree. Returns 1 on success. */
int rb_delete_node(RBTree *t, Node *z) {
    if (z == NULL || z == t->NIL) return 0;

    Node *y = z;
    Node *x;
    Color y_original_color = y->color;

    if (z->left == t->NIL) {
        x = z->right;
        transplant(t, z, z->right);
    } else if (z->right == t->NIL) {
        x = z->left;
        transplant(t, z, z->left);
    } else {
        y = tree_minimum(t, z->right); /* in-order successor */
        y_original_color = y->color;
        x = y->right;
        if (y->parent == z) {
            x->parent = y;
        } else {
            transplant(t, y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }
        transplant(t, z, y);
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
    }

    if (y_original_color == BLACK) delete_fixup(t, x);

    free(z);
    return 1;
}

int rb_delete_value(RBTree *t, int value) {
    Node *z = rb_search(t, t->root, value);
    if (!z) return 0;
    return rb_delete_node(t, z);
}

int rb_delete_min(RBTree *t) {
    if (t->root == t->NIL) return 0;
    Node *m = tree_minimum(t, t->root);
    return rb_delete_node(t, m);
}

int rb_delete_max(RBTree *t) {
    if (t->root == t->NIL) return 0;
    Node *m = tree_maximum(t, t->root);
    return rb_delete_node(t, m);
}

/* ---------------------------------------------------------- */
/*  Printing / Visualization                                    */
/* ---------------------------------------------------------- */

const char *rb_color_name(Color c) { return c == RED ? "RED" : "BLACK"; }

/* Structured, indented view of the tree showing each node as
 * (value)--(COLOR->depth), similar to the sample output format. */
static void print_structured(RBTree *t, Node *node, int depth) {
    if (node == t->NIL) return;
    print_structured(t, node->right, depth + 1);
    for (int i = 0; i < depth; i++) printf("        ");
    printf("(%d)--(%s->%d)\n", node->value, rb_color_name(node->color), depth);
    print_structured(t, node->left, depth + 1);
}

void rb_print(RBTree *t) {
    if (t->root == t->NIL) {
        printf("(empty tree)\n");
        return;
    }
    print_structured(t, t->root, 0);
}

/* Simple in-order listing: value(COLOR) value(COLOR) ... */
static void inorder(RBTree *t, Node *node) {
    if (node == t->NIL) return;
    inorder(t, node->left);
    printf("%d(%s) ", node->value, rb_color_name(node->color));
    inorder(t, node->right);
}

void rb_print_inorder(RBTree *t) {
    if (t->root == t->NIL) {
        printf("(empty tree)\n");
        return;
    }
    inorder(t, t->root);
    printf("\n");
}

/* ---------------------------------------------------------- */
/*  Validation (useful for testing the implementation)          */
/* ---------------------------------------------------------- */

static int validate_helper(RBTree *t, Node *node, int *bh_out) {
    if (node == t->NIL) {
        *bh_out = 1; /* NIL counts as one black node */
        return 1;
    }
    if (node->color == RED) {
        if (node->left->color == RED || node->right->color == RED) {
            printf("VIOLATION: red node %d has a red child\n", node->value);
            return 0;
        }
    }
    int lbh, rbh;
    int lok = validate_helper(t, node->left, &lbh);
    int rok = validate_helper(t, node->right, &rbh);
    if (!lok || !rok) return 0;
    if (lbh != rbh) {
        printf("VIOLATION: black-height mismatch at node %d (%d vs %d)\n",
               node->value, lbh, rbh);
        return 0;
    }
    *bh_out = lbh + (node->color == BLACK ? 1 : 0);
    return 1;
}

int rb_validate(RBTree *t) {
    if (t->root->color != BLACK) {
        printf("VIOLATION: root is not black\n");
        return 0;
    }
    int bh;
    return validate_helper(t, t->root, &bh);
}

/* ---------------------------------------------------------- */
/*  Memory cleanup                                              */
/* ---------------------------------------------------------- */

static void free_nodes(RBTree *t, Node *node) {
    if (node == t->NIL) return;
    free_nodes(t, node->left);
    free_nodes(t, node->right);
    free(node);
}

void rb_destroy(RBTree *t) {
    free_nodes(t, t->root);
    free(t->NIL);
    free(t);
}
