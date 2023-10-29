#pragma once

typedef struct _node {
    int imageSegment;
    char* buffer;
    struct _node* next;
} NODE;

typedef struct _stack {
    int stackSize;
    NODE* head;
} STACK;

void initStack(STACK* stack);
void push(STACK* stack, char* buffer, int imageSegment);
char* pop(STACK* stack);