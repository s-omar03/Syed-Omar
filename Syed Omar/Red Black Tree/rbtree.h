/*
 * ============================================================
 *  rbtree.h — Public API for the Red-Black Tree
 * ============================================================
 *  Include this header to use the Red-Black Tree from any
 *  program. The implementation lives in rbtree.c; the
 *  interactive CLI that exercises this API lives in main.c.
 * ============================================================
 */

#ifndef RBTREE_H
#define RBTREE_H

typedef enum { BLACK = 0, RED = 1 } Color;

typedef struct Node {
    int value;
    Color color;
    struct Node *left;
    struct Node *right;
    struct Node *parent;
} Node;

typedef struct {
    Node *root;
    Node *NIL;   /* sentinel node representing all NIL leaves (always BLACK) */
} RBTree;

/* ---- Construction / destruction ---------------------------- */
RBTree *rb_create(void);
void    rb_destroy(RBTree *t);

/* ---- Core operations ---------------------------------------- */
int   rb_insert(RBTree *t, int value);        /* 1 = inserted, 0 = duplicate */
int   rb_delete_value(RBTree *t, int value);  /* 1 = deleted,  0 = not found */
int   rb_delete_node(RBTree *t, Node *z);      /* 1 = deleted,  0 = invalid  */
Node *rb_search(RBTree *t, Node *x, int value);

/*Min / Max*/
Node *rb_find_min(RBTree *t);
Node *rb_find_max(RBTree *t);
int   rb_delete_min(RBTree *t);
int   rb_delete_max(RBTree *t);

/*Printing / visualization*/
void rb_print(RBTree *t);           /* structured, indented view */
void rb_print_inorder(RBTree *t);   /* flat left-to-right listing */

/*Validation (checks all 5 red-black properties)*/
int rb_validate(RBTree *t);

/*Small helper exposed for callers that want the color name*/
const char *rb_color_name(Color c);

#endif /* RBTREE_H */
