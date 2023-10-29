#include <stdio.h>
#include <stdlib.h>
#include "stack.h"

void initStack(STACK* stack){
    stack->head = NULL;
    stack->stackSize = 0;
}

void push(STACK* stack, char* buffer, int imageSegment){
    NODE* newNode = malloc(sizeof(NODE));
    newNode->buffer = buffer;
    newNode->imageSegment = imageSegment;

    NODE* temp = stack->head;
    stack->head = newNode;
    stack->head->next = temp;
}

// Might need?
void cleanup(STACK* stack){
    NODE* currNode = stack->head;

}

// Returns buffer address. ENSURE TO STORE RETURNED BUFFER OR MEMORY LEAK!!
char* popFront(STACK* stack){
    char* popped = stack->head->buffer;

    NODE* temp = stack->head;
    stack->head = stack->head->next;

    free(temp);

    return popped;
}

int main(int argc, char* argv[]){
    STACK stack;
    initStack(&stack);

    char* str = "HELLO WORLD";
    char* anotherString = "BREH";
    push(&stack, str, 2);
    push(&stack, anotherString, 4);

    char* buff = popFront(&stack);
    printf("%s\n", buff);

    buff = popFront(&stack);
    printf("%s\n", buff);

    return 0;
}