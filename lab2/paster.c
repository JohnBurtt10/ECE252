#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <curl/curl.h>

#define THREAD_CMD "-t"
#define RANDOM_PNG_SEG "-n"
#define BUF_SIZE 8000  /* 1024*1024 = 1M */
#define BUF_INC  500   /* 1024*512  = 0.5M */
#define ECE252_HEADER "X-Ece252-Fragment: "

#define max(a, b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

typedef struct _args {
    int numThreads;
    int imageNum;
} args;

typedef struct _thread_args {
    int thread_id;
    int imageToGrab;
} THREAD_ARGS;

typedef struct recv_buf2 {
    char *buf;       /* memory to hold a copy of received data */
    size_t size;     /* size of valid data in buf in bytes*/
    size_t max_size; /* max capacity of buf in bytes*/
    int seq;         /* >=0 sequence number extracted from http header */
                     /* <0 indicates an invalid seq number */
} RECV_BUF;

typedef struct _png_buffer {
    char* pngs[50]; // 50 images, each of a buffer of size BUF_SIZE (Aka some char* of size BUF_SIZE)
    volatile bool imageReceived[50];
    volatile int size;
} PNG_BUFFER;

void processInput(char *command, char *value, args* destination);
void initalizeArguments(args* argument);
// CURL* curlInit(THREAD_ARGS* threadArgs, RECV_BUF* recv_buf);
void* createRequest(void* args);
void initPNGBuffer(PNG_BUFFER* pngBuffer);
int recv_buf_init(RECV_BUF *ptr, size_t max_size);

PNG_BUFFER pngBuffer;

/* cURL callbacks */
// Callback that receives header data.
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata); 
// Callback for writing received data.
size_t write_cb_curl(char *p_recv, size_t size, size_t nmemb, void *p_userdata);

const char* const urls[3][3] = 
{
    // Server 1
    {"http://ece252-1.uwaterloo.ca:2520/image?img=1", 
    "http://ece252-1.uwaterloo.ca:2520/image?img=2", 
    "http://ece252-1.uwaterloo.ca:2520/image?img=3"},
    // Server 2
    {"http://ece252-2.uwaterloo.ca:2520/image?img=1", 
    "http://ece252-2.uwaterloo.ca:2520/image?img=2", 
    "http://ece252-2.uwaterloo.ca:2520/image?img=3"},
    // Server 3
    {"http://ece252-3.uwaterloo.ca:2520/image?img=1", 
    "http://ece252-3.uwaterloo.ca:2520/image?img=2", 
    "http://ece252-3.uwaterloo.ca:2520/image?img=3"}
};

pthread_mutex_t lock; // Mutex lock we will use to lock resources. Aka block other threads from executing some lines

int main(int argc, char* argv[]){

    args *arguments = malloc(sizeof(args));

    // Initalizing pngBuffer
    initPNGBuffer(&pngBuffer);

    curl_global_init(CURL_GLOBAL_DEFAULT);

    initalizeArguments(arguments);

    for (int i = 1; i < argc - 1; i = i + 2){
        processInput(argv[i], argv[i+1], arguments);
    }

    pthread_t* p_tids = malloc(sizeof(pthread_t) * arguments->numThreads);
    THREAD_ARGS* threadArgs = malloc(sizeof(THREAD_ARGS) * arguments->numThreads);


    // Initilizing Mutex lock
    if (pthread_mutex_init(&lock, NULL) != 0) {
        printf("\n mutex init has failed\n");
        return 1;
    }

    printf("numThreads: %d | imageSegment: %d\n", arguments->numThreads, arguments->imageNum);

    // Create threads
    for (int i = 0; i < arguments->numThreads; ++i){
        threadArgs[i].imageToGrab = arguments->imageNum;
        threadArgs[i].thread_id = i;
        pthread_create(p_tids + i, NULL, &createRequest, threadArgs + i); 
    }

    // Join threads
    for (int i = 0; i < arguments->numThreads; ++i){
        pthread_join(p_tids[i], NULL);
    }

    free(arguments);
    free(p_tids);
    free(threadArgs);

    for (int i = 0; i < 50; i++){
        free(pngBuffer.pngs[i]);
    }

    curl_global_cleanup();
    return 0;
}

/* 
1) Call curl_easy_perform to call webserver, 
2) extract data and header 
3) Based on ECE252_HEADER line received, extract the image sequence number from it
4) insert png data to proper index on pngArray buff depending on sequence number
5) increment pngArray counter.
*/
void* createRequest(void* args){
    THREAD_ARGS* threadArgs = (THREAD_ARGS*) args;
    RECV_BUF recv_buf;

    CURL* curl_handle;
    curl_handle = curl_easy_init();

    if (curl_handle == NULL){
        printf("Failed to initalize CURL");
        return NULL;
    }    

    int imageToGrab = threadArgs->imageToGrab - 1;

    /* specify URL to get */
    curl_easy_setopt(curl_handle, CURLOPT_URL, urls[0][imageToGrab]);

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

    while (pngBuffer.size < 50){
        recv_buf_init(&recv_buf, BUF_SIZE);
        CURLcode res = curl_easy_perform(curl_handle);

        if( res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        } else {
            printf("%lu bytes received in memory %p, seq=%d.\n", \
                recv_buf.size, recv_buf.buf, recv_buf.seq);
        }

        // Has image, write to pngbuffer
        pthread_mutex_lock(&lock);
        // Checking if image is already received
        if (pngBuffer.imageReceived[recv_buf.seq] == false){
            pngBuffer.pngs[recv_buf.seq] = recv_buf.buf;
            pngBuffer.imageReceived[recv_buf.seq] = true;
            pngBuffer.size++;
        } else {
            free((&recv_buf)->buf);
        }
        pthread_mutex_unlock(&lock);
    }

    curl_easy_cleanup(curl_handle);

    return NULL;
}

// Sets up curl handler. NOT USED
CURL* curlInit(THREAD_ARGS* threadArgs, RECV_BUF* recv_buf){

    CURL* curl_handle;
    curl_handle = curl_easy_init();

    if (curl_handle == NULL){
        printf("Failed to initalize CURL");
        return NULL;
    }    

    int imageToGrab = threadArgs->imageToGrab - 1;

    printf("%s\n", urls[0][imageToGrab]);

    /* specify URL to get */
    curl_easy_setopt(curl_handle, CURLOPT_URL, urls[threadArgs->thread_id % 3][imageToGrab]);

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

    return curl_handle;

}

// ptr points to the delivered data, and the size of that data is nmemb; size is always 1. 
size_t write_cb_curl(char *p_recv, size_t size, size_t nmemb, void *p_userdata){
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;
 
    if (p->size + realsize + 1 > p->max_size) {/* hope this rarely happens */ 
        /* received data is not 0 terminated, add one byte for terminating 0 */
        size_t new_size = p->max_size + realsize + max(BUF_INC, realsize + 1);   
        char *q = realloc(p->buf, new_size);
        if (q == NULL) {
            perror("realloc"); /* out of memory */
            return -1;
        }
        p->buf = q;
        p->max_size = new_size;
    }

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

/* Takes in the command, the value associated to the command, and a destination "args" to save results to. */
void processInput(char *command, char *value, args* destination){
    long stringToLong = strtol(value, NULL, 10);
    
    int longToInt = stringToLong;

    if (strcmp(command, THREAD_CMD) == 0){
        destination->numThreads = longToInt;
    }

    if (strcmp(command, RANDOM_PNG_SEG) == 0){
        destination->imageNum = longToInt;
    }
}

void initalizeArguments(args* argument){
    argument->imageNum = 1;
    argument->numThreads = 1;
}

void initPNGBuffer(PNG_BUFFER* pngBuffer){
    for (int i = 0; i < 50; ++i){
        pngBuffer->imageReceived[i] = false;
    }
    pngBuffer->size = 0;
}

int recv_buf_init(RECV_BUF *ptr, size_t max_size)
{
    void *p = NULL;
    
    if (ptr == NULL) {
        return 1;
    }

    p = malloc(max_size);
    if (p == NULL) {
	return 2;
    }
    
    ptr->buf = p;
    ptr->size = 0;
    ptr->max_size = max_size;
    ptr->seq = -1;              /* valid seq should be non-negative */
    return 0;
}