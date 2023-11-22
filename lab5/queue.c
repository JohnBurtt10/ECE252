#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

void init_queue(QUEUE* queue){
    queue->head = NULL;
    queue->queueSize = 0;
}

void push_back(QUEUE* queue, char* url){
    NODE* newNode = malloc(sizeof(NODE));
    newNode->buffer = url;

    NODE* temp = queue->head;
    queue->head = newNode;
    queue->head->next = temp;
    queue->queueSize++;
}

void clean_queue(QUEUE* queue){
    NODE* currNode = queue->head;
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
char* pop_front(QUEUE* queue){
    if (queue->queueSize == 0){
        puts("Nothing to Pop");
        return NULL;
    }
    char* popped = queue->head->buffer;

    NODE* temp = queue->head;
    queue->head = queue->head->next;

    queue->queueSize--;

    free(temp);

    return popped;
}

int is_empty(QUEUE* queue){
    return (queue->queueSize == 0);
}

void print_queue(QUEUE* queue, const char *filename){
 FILE* file = fopen(filename, "w");

    if (file == NULL) {
        perror("Error opening file");
        return;
    }

    NODE* p_currNode = queue->head;
    int counter = 0;

    while (p_currNode != NULL) {
        // printf("%s\n", p_currNode->buffer);
        fprintf(file, "%s\n", p_currNode->buffer);
        p_currNode = p_currNode->next;
        counter++;
    }

    fclose(file);
}

int get_queueSize(QUEUE* queue){
    return queue->queueSize;
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