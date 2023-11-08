#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

void init_queue(QUEUE* stack){
    stack->head = NULL;
    stack->stackSize = 0;
}

void push_back(QUEUE* stack, char* url){
    NODE* newNode = malloc(sizeof(NODE));
    newNode->buffer = url;

    NODE* temp = stack->head;
    stack->head = newNode;
    stack->head->next = temp;
    stack->stackSize++;
}

void clean_queue(QUEUE* stack){
    NODE* currNode = stack->head;
    NODE* temp;
    for (int i = 0; i < stack->stackSize; ++i){
        temp = currNode;
        currNode = currNode->next;
        free(temp);
    }
}

// Returns buffer address. ENSURE TO STORE RETURNED BUFFER OR MEMORY LEAK!!
char* pop_front(QUEUE* stack){
    if (stack->stackSize == 0){
        puts("Nothing to Pop");
        return NULL;
    }
    char* popped = stack->head->buffer;

    NODE* temp = stack->head;
    stack->head = stack->head->next;

    stack->stackSize--;

    free(temp);

    return popped;
}

int is_empty(QUEUE* queue){
    return (queue->stackSize == 0);
}

// int main(int argc, char* argv[]){
//     QUEUE stack;
//     initStack(&stack);

//     char* str = "HELLO WORLD";
//     char* anotherString = "BREH";
//     char* someotherString = "TEST";
//     push(&stack, str);
//     push(&stack, anotherString);
//     push(&stack, someotherString);

//     char* url = popFront(&stack);
//     printf("%s\n", url);

//     url = popFront(&stack);
//     printf("%s\n", url);

//     clean_queue(&stack);

//     return 0;
// }