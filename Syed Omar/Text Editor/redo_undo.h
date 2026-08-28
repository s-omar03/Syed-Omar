#ifndef REDO_UNDO_H
#define REDO_UNDO_H

#include "text_editor.h"

void stackInit(DynamicArrayStack *stack);
void stackPush(DynamicArrayStack *stack, ActionType type, const char *text, int line, int pos);
int stackPop(DynamicArrayStack *stack, Action *out);
void stackClear(DynamicArrayStack *stack);
void stackFree(DynamicArrayStack *stack);

void recordAction(ActionType type, const char *text, int line, int pos);
void performUndo(void);
void performRedo(void);

#endif /* REDO_UNDO_H */
