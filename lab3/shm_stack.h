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
    size_t size;     /* size of valid data in buf in bytes*/
    int seq;         /* >=0 sequence number extracted from http header */
                     /* <0 indicates an invalid seq number */
    unsigned char buf[10000];       /* Image cannot exceed 10,000 bytes */
} RECV_BUF;

typedef struct image_stack
{
    int size;               /* the max capacity of the stack */
    int pos;                /* position of last item pushed onto the stack */
    int imageSeqPos;        /* position of last image sequence pushed onto the stack */
    int* imageSeq;          /* Image Sequence*/
    unsigned char* imageData;        /* Image Data */
    sem_t imageToFetch_sem; // Allow one producer at a time to increment image counter.
    sem_t buffer_sem; // Allow one process at a time to acccess stack/buffer.
    sem_t items_sem;  // track amount of items currently in buffer/stack to block consumers
    sem_t spaces_sem; // Track amount of free space in buffer to block producers
    sem_t pushImage_sem; // Allow one process at a time to access the decompressed image buffer
    sem_t consumedCount_sem; // Track how many images the consumers have produced. If 0, consumers are done.
} IMGSTACK;


int sizeof_shm_stack(int size);
int init_shm_stack(struct image_stack *p, int stack_size);
struct image_stack *create_stack(int size);
void destroy_stack(struct image_stack *p);
int is_full(struct image_stack *p);
int is_empty(struct image_stack *p);
int push(struct image_stack *p, RECV_BUF* image);
int pop(struct image_stack *p, RECV_BUF* image);
