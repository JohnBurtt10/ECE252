#include <semaphore.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include "shm_stack.h"
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <curl/curl.h>

#define INCRE_SEM "/increSem"
#define PROD_SEM "/prodSem" 
#define CON_SEM "/conSem"

typedef struct _args {
    int bufferSize; // Buffer to hold image segments. Less than 50.
    int numProducers; // Number of producers to produce the data.
    int numConsumers; // Number of consumers to consume/process the data.
    int numMilliseconds; // Number milliseconds for the consumer to sleep for after processing/consuming the data.
    int imageToFetch; // Which image segment to fetch
} args; 

const char* const urls[3][3] = 
{
    // Server 1
    {"http://ece252-1.uwaterloo.ca:2530/image?img=1&part= ", 
    "http://ece252-1.uwaterloo.ca:2530/image?img=1&part= ", 
    "http://ece252-1.uwaterloo.ca:2530/image?img=1&part= "},
    // Server 2
    {"http://ece252-2.uwaterloo.ca:2530/image?img=1&part= ", 
    "http://ece252-2.uwaterloo.ca:2530/image?img=2&part= ", 
    "http://ece252-2.uwaterloo.ca:2530/image?img=3&part= "},
    // Server 3
    {"http://ece252-3.uwaterloo.ca:2530/image?img=1&part= ", 
    "http://ece252-3.uwaterloo.ca:2530/image?img=2&part= ", 
    "http://ece252-3.uwaterloo.ca:2530/image?img=3&part= "}
};

args arguments;

sem_t *sem;

unsigned int count = 0;

int checkAndSet() {
    unsigned int return_val;
    if (count > 50) { 
        return -1;
    }
    return_val = count;
    count++;
    return return_val;
}

void processInput(int argc, char *argv[], args* destination);

int main(int argc, char* argv[]){
    unsigned int itt;
    processInput(argc, argv, &arguments);
    unsigned int pid; 

    struct int_stack* stack;
    int shm_size =  sizeof_shm_stack(arguments.bufferSize);
    int shmid = shmget(IPC_PRIVATE, shm_size, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);    

    for (int i = 0; i < arguments.numProducers; ++i){
        pid = fork();
        if (getpid == 0) {
            // child process
            sem = sem_open(INCRE_SEM, O_CREAT | O_EXCL, 0644, 1);
            if (sem == SEM_FAILED) {
                perror("sem_open");
                exit(1);
            }
            struct int_stack *pstack;    
            pstack = shmat(shmid, NULL, 0);
            if ( pstack == (void *) -1 ) {
                perror("shmat");
                abort();
            }

            // waitpid(cpid, NULL, 0);
        // pop_all(pstack);
            if ( shmdt(pstack) != 0 ) {
                perror("shmdt");
                abort();
            }
            /* We do not use free() to release the shared memory, use shmctl() */
            if ( shmctl(shmid, IPC_RMID, NULL) == -1 ) {
                perror("shmctl");
                abort();
            }
            while (1) { 
                sem_wait(sem);
                itt = checkAndSet();
                sem_post(sem);
                if (itt == -1) exit(0);
                
            }
            
        }
    }

    for (int i =0; i < arguments.numConsumers; ++i){

    }

    // Wait for all processes to end
    while(wait(NULL) > 0){};

    // deallocate memory


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

// void* createRequest(void* args){
//     THREAD_ARGS* threadArgs = (THREAD_ARGS*) args;
void* createRequest(unsigned int imageSequence){
    // THREAD_ARGS* threadArgs = (THREAD_ARGS*) args;
    RECV_BUF recv_buf;

    CURL* curl_handle;
    curl_handle = curl_easy_init();

    if (curl_handle == NULL){
        printf("Failed to initalize CURL");
        return NULL;
    }    

    int imageToGrab = imageSequence;

    // printf("Thread # %d fetching from webserver: %s\n", threadArgs->thread_id, urls[threadArgs->thread_id%3][imageToGrab]);

    /* specify URL to get */
    curl_easy_setopt(curl_handle, CURLOPT_URL, urls[get_pid()%3][imageToGrab]);

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

        recv_buf_init(&recv_buf, BUF_SIZE);
        CURLcode res = curl_easy_perform(curl_handle);

        // Has image, write to pngbuffer
        pthread_mutex_lock(&lock);
        // Checking if image is already received
        // if (pngBuffer.imageReceived[recv_buf.seq] == false){
        //     pngBuffer.pngs[recv_buf.seq] = recv_buf.buf;
        //     pngBuffer.imageReceived[recv_buf.seq] = true;
        //     pngBuffer.size++;
        // } else {
        //     free((&recv_buf)->buf);
        // }
        stack
        pthread_mutex_unlock(&lock);

    curl_easy_cleanup(curl_handle);

    return NULL;
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
