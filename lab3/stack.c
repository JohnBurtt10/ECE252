#include <stdio.h>
#include <stack.h>

void initStack(STACK* stack){
    stack->head = NULL;
    stack->stackSize = 0;
}

void push(STACK* stack, char* buffer, int imageSegment){
    NODE* newNode = malloc(sizeof(NODE));
    newNode->buffer = buffer;
    newNode->imageSegment = imageSegment;
    if (stack->head = NULL){
        stack->head = newNode;
        newNode->next = NULL;
    } else {
        NODE* temp = stack->head;
        stack->head = newNode;
        newNode->next = temp;
        temp = NULL;
    }
}

// Might need?
void cleanup(STACK* stack){
    NODE* currNode = stack->head;

}

// Returns buffer address. ENSURE TO STORE RETURNED BUFFER OR MEMORY LEAK!!
char* popFront(STACK* stack){
    NODE* popped = stack->head;

    stack->head = stack->head->next;

    free(popped);

    return popped->buffer;
}