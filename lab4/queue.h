#pragma once

typedef struct _node {
    int imageSegment;
    char* buffer;
    struct _node* next;
} NODE;

typedef struct _queue {
    volatile int queueSize;
    NODE* head;
} QUEUE;

void init_queue(QUEUE* queue);
void push_back(QUEUE* queue, char* buffer);
char* pop_front(QUEUE* queue);
int is_empty(QUEUE* queue);
void print_queue(QUEUE* queue);
void clean_queue(QUEUE* queue);
int get_queueSize(QUEUE* queue);