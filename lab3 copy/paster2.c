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
void destory_sems();
int producerCheckAndSet();
void createRequest(unsigned int imageSequence);

int recv_buf_init(RECV_BUF *ptr);
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata); 
size_t write_cb_curl(char *p_recv, size_t size, size_t nmemb, void *p_userdata);

/* Attached memory variables */
IMGSTACK* pstack;
int* imageToFetch;
/* Shared memory ID's */
int shmid_stack;
int shmid_imageToFetch;

int main(int argc, char* argv[]){
    processInput(argc, argv, &arguments);

    // Testing stack
    //test_stack();

    // Creating shared stack and initalizing it.
    int shm_size = sizeof_shm_stack(arguments.bufferSize);
    shmid_stack = shmget(IPC_PRIVATE, shm_size, IPC_CREAT | 0666);
    pstack = shmat(shmid_stack, NULL, 0);
    if (init_shm_stack(pstack, arguments.bufferSize)){
        perror("Failed to create stack");
        exit(1);
    };
    init_shm_stack(pstack, arguments.bufferSize);

    // Creating image counter to keep track of current image to fetch
    shmid_imageToFetch = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    imageToFetch = shmat(shmid_imageToFetch, NULL, 0);
    *imageToFetch = 0;

    // Create producers
    for(int i = 0; i < arguments.numProducers; ++i){
        int pid = fork();
        
        // child
        if (pid == 0){
            int itt = 0;
            itt = producerCheckAndSet();
            
            if (itt == -1){
                exit(0);
            }



            exit(0);
        }
    }

    // Wait for all processes to finish
    while(wait(NULL) > 0);

    // Cleaning up
    shmdt(pstack);
    shmctl(shmid_stack, IPC_RMID, NULL);

    destory_sems();

    return 0;
}

void destory_sems(){
    sem_destroy(&pstack->buffer_sem);
    sem_destroy(&pstack->spaces_sem);
    sem_destroy(&pstack->imageToFetch_sem);
    sem_destroy(&pstack->items_sem);
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

int producerCheckAndSet() {
    sem_wait(&pstack->imageToFetch_sem);
    unsigned int return_val;
    if (*imageToFetch >= 50) { 
        return -1;
    }
    return_val = *imageToFetch;
    *imageToFetch = *imageToFetch + 1;
    return return_val;
    sem_post(&pstack->imageToFetch_sem);
}

void createRequest(unsigned int image_segment){
    RECV_BUF recv_buf;

    CURL* curl_handle;
    curl_handle = curl_easy_init();

    if (curl_handle == NULL){
        printf("Failed to initalize CURL");
        return;
    }    

    char url[60];

    sprintf(url, "%s%d", urls[image_segment%3][arguments.imageToFetch-1], image_segment);

    printf("processID # %d fetching image strip: %d\n", getpid(), image_segment);

    /* specify URL to get */
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);

    /* register write call back function to process received data */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&recv_buf);

    /* register header call back function to process received header data */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)&recv_buf);

    /* some servers requires a user-agent field */
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    CURLcode res = curl_easy_perform(curl_handle);

    curl_easy_cleanup(curl_handle);

    return;
}

// ptr points to the delivered data, and the size of that data is nmemb; size is always 1. 
size_t write_cb_curl(char *p_recv, size_t size, size_t nmemb, void *p_userdata){
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;

    memcpy(p->buf + p->size, p_recv, realsize); /*copy data from libcurl*/
    p->size += realsize;
    p->buf[p->size] = 0;

    return realsize;
}

size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata){
    int realsize = size * nmemb;
    RECV_BUF *p = userdata;
    
    if (realsize > strlen(ECE252_HEADER) && strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0) {
        /* extract img sequence number */
	    p->seq = atoi(p_recv + strlen(ECE252_HEADER));
    }

    return realsize;
}

void test_stack(){
    RECV_BUF** array;
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

    printf("Is Empty: %d\n", is_empty(pstack));

    shmdt(pstack);
    shmctl(shmid_stack, IPC_RMID, NULL);

    for (int i = 0; i < arguments.bufferSize; ++i){
        free(array[i]);
    }

    free(array);
}