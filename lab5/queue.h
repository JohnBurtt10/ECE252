#pragma once

// provided in starter code.
typedef struct recv_buf2
{
    char *buf;       /* memory to hold a copy of received data */
    size_t size;     /* size of valid data in buf in bytes*/
    size_t max_size; /* max capacity of buf in bytes*/
    int seq;         /* >=0 sequence number extracted from http header */
                     /* <0 indicates an invalid seq number */
    int curl_id;
} RECV_BUF;

typedef struct _node {
    int imageSegment;
    char* buffer;
    RECV_BUF* recv_buf;
    struct _node* next;
} NODE;

typedef struct _queue {
    volatile int queueSize;
    NODE* head;
} QUEUE;

void init_queue(QUEUE* queue);
void push_back(QUEUE* queue, char* url, RECV_BUF* recv_buf);
char* pop_front(QUEUE* queue);
RECV_BUF* pop_front_recv_buf(QUEUE* queue);
int is_empty(QUEUE* queue);
void print_queue(QUEUE* queue, const char *filename);
void clean_queue(QUEUE* queue);
int get_queueSize(QUEUE* queue);