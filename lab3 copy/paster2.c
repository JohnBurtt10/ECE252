#include <semaphore.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include "shm_stack.h"
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <curl/curl.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>

#include "../lab1/starter/png_util/zutil.h"    /* for mem_def() and mem_inf() */

#define INCRE_SEM "/increSemr"
#define CON_SEM "/conSem3asd"
#define BUFFER_SEM "/bufferSem323w"
#define ITEMS_SEM "/itemsSem1000"
#define SPACES_SEM "/SpacesSem312"

#define BUF_SIZE 10000  /* 1024*1024 = 1M */

#define BUF_INC  500   /* 1024*512  = 0.5M */

#define IDAT_OFFSET_BYTES 33 // 33 bytes from start of file to get IDAT chunk start.

#define IHDR_CHUNK_SIZE 25

#define HEADER_SIZE 8

#define ECE252_HEADER "X-Ece252-Fragment: "

#define max(a, b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })


typedef struct _args {
    int bufferSize; // Buffer to hold image segments. Less than 50.
    int numProducers; // Number of producers to produce the data.
    int numConsumers; // Number of consumers to consume/process the data.
    int numMilliseconds; // Number milliseconds for the consumer to sleep for after processing/consuming the data.
    int imageToFetch; // Which image segment to fetch
} args; 

char* urls[3][3] = 
{
    // Server 1
    {"http://ece252-1.uwaterloo.ca:2530/image?img=1&part=", 
    "http://ece252-1.uwaterloo.ca:2530/image?img=2&part=", 
    "http://ece252-1.uwaterloo.ca:2530/image?img=3&part="},
    // Server 2
    {"http://ece252-2.uwaterloo.ca:2530/image?img=1&part=", 
    "http://ece252-2.uwaterloo.ca:2530/image?img=2&part=", 
    "http://ece252-2.uwaterloo.ca:2530/image?img=3&part="},
    // Server 3
    {"http://ece252-3.uwaterloo.ca:2530/image?img=1&part=", 
    "http://ece252-3.uwaterloo.ca:2530/image?img=2&part=", 
    "http://ece252-3.uwaterloo.ca:2530/image?img=3&part="}
};

args arguments;

void signal_handler(int sig);

void processInput(int argc, char *argv[], args* destination);

int recv_buf_init(RECV_BUF *ptr);

// Shared memory stuff
IMGSTACK* pstack;
RECV_BUF** array;

int main(int argc, char* argv[]){
    processInput(argc, argv, &arguments);

    array = malloc(sizeof(RECV_BUF) * arguments.bufferSize);

    // Create shared memory.
    int shm_size =  sizeof_shm_stack(arguments.bufferSize);
    int shmid_stack = shmget(IPC_PRIVATE, shm_size, IPC_CREAT | 0666); 
    pstack = shmat(shmid_stack, NULL, 0);
    init_shm_stack(pstack, arguments.bufferSize);

    for (int i = 0; i < arguments.bufferSize; ++i){
        RECV_BUF* newBuffer = malloc(sizeof(RECV_BUF));
        array[i] = newBuffer;
        sprintf(array[i]->buf, "blahlbafdasds%d", i);
        array[i]->seq = i;
        array[i]->size = 10000;

        push(pstack, array[i]);
    }

    int testPush = push(pstack, array[0]);

    printf("TestPush: %d\n", testPush);

    for (int i = 0; i < arguments.bufferSize; ++i){
        RECV_BUF test;
        pop(pstack, &test);
        printf("Image seq: %d, image buffer: %s\n", test.seq, test.buf);
    }

    shmdt(pstack);
    shmctl(shmid_stack, IPC_RMID, NULL);

    for (int i = 0; i < arguments.bufferSize; ++i){
        free(array[i]);
    }

    free(array);

    return 0;
}


void processInput(int argc, char *argv[], args* destination){
    if (argc < 6){
        printf("Missing commands.");
        exit(1);
    }

    destination->bufferSize = strtoul(argv[1], NULL, 10);
    destination->numProducers = strtoul(argv[2], NULL, 10);
    destination->numConsumers = strtoul(argv[3], NULL, 10);
    destination->numMilliseconds = strtoul(argv[4], NULL, 10);
    destination->imageToFetch = strtoul(argv[5], NULL, 10);
}

int recv_buf_init(RECV_BUF *ptr)
{
    ptr->buf[10000];
    ptr->size = 0;
    ptr->seq = -1;              /* valid seq should be non-negative */
    return 0;
}