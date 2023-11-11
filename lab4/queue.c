#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

void init_queue(QUEUE* stack){
    stack->head = NULL;
    stack->queueSize = 0;
}

void push_back(QUEUE* stack, char* url){
    NODE* newNode = malloc(sizeof(NODE));
    newNode->buffer = url;

    NODE* temp = stack->head;
    stack->head = newNode;
    stack->head->next = temp;
    stack->queueSize++;
}

void clean_queue(QUEUE* stack){
    NODE* currNode = stack->head;
    NODE* temp;
    while(currNode != NULL){
        temp = currNode;
        currNode = currNode->next;
        if (temp->buffer != NULL){
            free(temp->buffer);
        }
        free(temp);
    }
}

// Returns buffer address. ENSURE TO STORE RETURNED BUFFER OR MEMORY LEAK!!
char* pop_front(QUEUE* stack){
    if (stack->queueSize == 0){
        puts("Nothing to Pop");
        return NULL;
    }
    char* popped = stack->head->buffer;

    NODE* temp = stack->head;
    stack->head = stack->head->next;

    stack->queueSize--;

    free(temp);

    return popped;
}

int is_empty(QUEUE* queue){
    return (queue->queueSize == 0);
}

void print_queue(QUEUE* queue){
    NODE* p_currNode = queue->head;
    int counter = 0;
    while (p_currNode != NULL){
        printf("Node: %d, value: %s\n", counter, p_currNode->buffer);
        p_currNode = p_currNode->next;
        counter++;
    }
}

// int main(int argc, char* argv[]){
//     QUEUE queue;
//     init_queue(&queue);

//     char* str = "HELLO WORLD";
//     char* anotherString = "BREH";
//     char* someotherString = "TEST";
//     push_back(&queue, str);
//     push_back(&queue, anotherString);
//     push_back(&queue, someotherString);

//     print_queue(&queue);

//     char* url = pop_front(&queue);
//     printf("%s\n", url);

//     url = pop_front(&queue);
//     printf("%s\n", url);

//     clean_queue(&queue);

//     return 0;
// }