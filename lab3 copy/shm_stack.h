
/* The code is 
 * Copyright(c) 2018-2019 Yiqing Huang, <yqhuang@uwaterloo.ca>.
 *
 * This software may be freely redistributed under the terms of the X11 License.
 */
/**
 * @brief  stack to push/pop integer(s), API header  
 * @author yqhuang@uwaterloo.ca
 */

typedef struct recv_buf2 {
    char buf[10000];       /* Image cannot exceed 10,000 bytes */
    size_t size;     /* size of valid data in buf in bytes*/
    int seq;         /* >=0 sequence number extracted from http header */
                     /* <0 indicates an invalid seq number */
} RECV_BUF;

typedef struct image_stack
{
    int size;               /* the max capacity of the stack */
    int pos;                /* position of last item pushed onto the stack */
    int imageSeqPos;        /* position of last image sequence pushed onto the stack */
    int* imageSeq;          /* Image Sequence*/
    char* imageData;        /* Image Data */
    sem_t imageToFetch_sem;
    sem_t buffer_sem;
    sem_t items_sem;
    sem_t spaces_sem;
} IMGSTACK;


int sizeof_shm_stack(int size);
int init_shm_stack(struct image_stack *p, int stack_size);
struct image_stack *create_stack(int size);
void destroy_stack(struct image_stack *p);
int is_full(struct image_stack *p);
int is_empty(struct image_stack *p);
int push(struct image_stack *p, RECV_BUF* image);
int pop(struct image_stack *p, RECV_BUF* image);
