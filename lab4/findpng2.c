#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <search.h> // Library used to include hash table.
#include <pthread.h>
#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/uri.h>

/* 
To insert into hashtable, use hsearch(ENTRY entry, ACTION action), where action = ENTER
*/

typedef struct _args {
    int numThreads;
    int numUniqueURLs;
    char* logFile;
    char* startingURL;
} ARGS;

// Utilize a hashmap to store already visited webpages to prevent visiting the same webpage more than once.
typedef struct _sharedVariables{
    ENTRY e, *ep;
} sharedVariables;

ARGS arguments;
sharedVariables shared_thread_variables;

int processInput(ARGS* arguments, char* argv[], int argc);
void initThreadStuff();
void* threadFunction(void* args);

int main(int argc, char* argv[]){
    processInput(&arguments, argv, argc);

    pthread_t* p_tids = malloc(sizeof(pthread_t) * arguments.numThreads);

    // Create hashTable. Table will store URL addresses and a flag to indicate if visited.

    for (int i = 0; i < arguments.numThreads; ++i){
        pthread_create(p_tids + i, NULL, &threadFunction, NULL);
    }

    // Join threads
    for (int i = 0; i < arguments.numThreads; ++i){
        pthread_join(p_tids[i], NULL);
    }

    printf("numThreads: %d, numUniquePngs: %d, logFile: %s, startingURL: %s\n", arguments.numThreads, arguments.numUniqueURLs, arguments.logFile, arguments.startingURL);

    hdestroy();
    return 0;
}

void* threadFunction(void* args){

    return NULL;
}

void initThreadStuff(){
    int status = hcreate(arguments.numUniqueURLs);
    if (status == 0){
        puts("Failed to initalize hash table. Exiting...");
        exit(1);
    }

    // Inserting seed url as the first webpage (NOT VISITED YET).
    shared_thread_variables.e.key = arguments.startingURL;
    shared_thread_variables.e.data = (void *) 0;
    shared_thread_variables.ep = hsearch(shared_thread_variables.e, ENTER);
    if (shared_thread_variables.ep == NULL) { // Failed to insert for whatever reason.
        fprintf(stderr, "entry failed\n");
        exit(EXIT_FAILURE);
    }
}

int processInput(ARGS* arguments, char* argv[], int argc){
    int c;
    arguments->numUniqueURLs = 50;
    arguments->numThreads = 1;
    char *str = "option requires an argument";

    arguments->startingURL = argv[argc - 1];

    while ((c = getopt (argc, argv, "t:m:v:")) != -1) {
        switch (c) {
        case 't':
            arguments->numThreads = atoi(optarg);
	        if (arguments->numThreads <= 0) {
                fprintf(stderr, "%s: %s > 0 -- 't'\n", argv[0], str);
                return -1;
            }
            break;
        case 'm':
            arguments->numUniqueURLs = atoi(optarg);
            if (arguments->numUniqueURLs < 0) {
                fprintf(stderr, "%s: %s that is greater than 0\n", argv[0], str);
                return -1;
            }
            break;
        case 'v':
            arguments->logFile = optarg;
            break;
        default:
            return -1;
        }
    }

    return -1;
}