#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <curl/curl.h>

#define THREAD_CMD "-t"
#define RANDOM_PNG_SEG "-n"

typedef struct _args {
    int numThreads;
    int imageNum;
} args;

typedef struct recv_buf2 {
    char *buf;       /* memory to hold a copy of received data */
    size_t size;     /* size of valid data in buf in bytes*/
    size_t max_size; /* max capacity of buf in bytes*/
    int seq;         /* >=0 sequence number extracted from http header */
                     /* <0 indicates an invalid seq number */
} RECV_BUF;

void processInput(char *command, char *value, args* destination);
void initalizeArguments(args* argument);
void curlInit(CURL* curlHandlers[], args* arguments);
void createRequest();
void cleanupCurlHandlers(CURL* curlArr[], int numOfCurls);
void setup_curl_handler(CURL* curlHandler);

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

int main(int argc, char* argv[]){

    args *arguments = malloc(sizeof(args));
    RECV_BUF* recvBuffer[3]; // There are 3 urls.

    curl_global_init(CURL_GLOBAL_DEFAULT);

    initalizeArguments(arguments);

    for (int i = 1; i < argc - 1; i = i + 2){
        processInput(argv[i], argv[i+1], arguments);
    }

    pthread_t *p_tids = malloc(sizeof(pthread_t) * arguments->numThreads);

     // Creating numThreads amount of curl handlers to give to each thread. Want to reuse handlers to improve performance.
    CURL* curlHandlers = malloc(sizeof(CURL) * arguments->numThreads);

    printf("numThreads: %d | imageSegment: %d\n", arguments->numThreads, arguments->imageNum);

    curlInit(curlHandlers, arguments);

    free(arguments);
    free(p_tids);
    // free(imageSegBuffer);
    cleanupCurlHandlers(curlHandlers, arguments->numThreads);
    curl_global_cleanup();
    free(curlHandlers);
    return 0;
}

void createRequest(){

}

// Sets up curl handler for provided url and png data.
void curlInit(CURL* curlHandlers[], args* arguments){

    for (int i = 0; i < arguments->numThreads; i++){
        curlHandlers[i] = curl_easy_init();

        if (curlHandlers[i] == NULL){
            printf("Failed to initalize CURL");
            return;
        }    

        int imageToGrab = arguments->imageNum - 1;

        printf("%s\n", urls[i % 3][imageToGrab]);

        /* specify URL to get */
        curl_easy_setopt(curlHandlers[i], CURLOPT_URL, urls[i % 3][imageToGrab]);

        /* register write call back function to process received data */
        // curl_easy_setopt(curlHandlers, CURLOPT_WRITEFUNCTION, write_cb_curl); 
        // /* user defined data structure passed to the call back function */
        // curl_easy_setopt(curlHandlers, CURLOPT_WRITEDATA, (void *)&pngData);

        /* register header call back function to process received header data */
        // curl_easy_setopt(curlHandlers, CURLOPT_HEADERFUNCTION, header_cb_curl); 
        // /* user defined data structure passed to the call back function */
        // curl_easy_setopt(curlHandlers, CURLOPT_HEADERDATA, (void *)&pngData);

        /* some servers requires a user-agent field */
        curl_easy_setopt(curlHandlers[i], CURLOPT_USERAGENT, "libcurl-agent/1.0");
    }
}

size_t write_cb_curl(char *p_recv, size_t size, size_t nmemb, void *p_userdata){

    return 0;
}

size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata){
    
    return 0;
}

void cleanupCurlHandlers(CURL* curlArr[], int numOfCurls){
    for(int i = 0; i < numOfCurls; i++){
        curl_easy_cleanup(curlArr[i]);
    }
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