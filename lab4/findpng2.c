#define _GNU_SOURCE
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
#include "queue.h"
#include <semaphore.h>

/* 
To insert into hashtable, use hsearch(ENTRY entry, ACTION action), where action = ENTER
*/

/* 
Frontier list: List to still search
hash table: Visited URL tracker.

Need to output png_urls.txt file that contains urls with a png
Need to putput another .txt file that contains urls that are NOT pngs.

Need two mutexes. One to protect the queue to allow only one thread to insert/pop and another to protect the hashtable.

use conditional signal to not waste CPU cycles on checking if the Queue is empty
*/

#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 1048576  /* 1024*1024 = 1M */
#define BUF_INC  524288   /* 1024*512  = 0.5M */

#define CT_PNG  "image/png"
#define CT_HTML "text/html"
#define CT_PNG_LEN  9
#define CT_HTML_LEN 9

#define MAX_NUM_ATTEMPTS 5

#define max(a, b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

typedef struct _args {
    int numThreads;
    int numUniqueURLs;
    char* logFile;
    char* startingURL;
} ARGS;

// Utilize a hashmap to store already visited webpages to prevent visiting the same webpage more than once.
typedef struct _sharedVariables{
    ENTRY e, *ep;   // Hash table of every site seen. Keeps track of visited or not visited
    QUEUE frontier; // To-visit urls
    QUEUE png_urls; // URLS that are pngs.
    QUEUE all_urls; // A queue used to store all URLS for deallocation.
    volatile int num_active_threads; // Keep tracking of number of threads actively crawling. // WONT BE NEEDED
    pthread_cond_t cond_variable;
    pthread_mutex_t queue_lock;
    pthread_mutex_t hash_lock;
    pthread_mutex_t cond_lock;
    pthread_mutex_t process_data_lock;
    pthread_mutex_t process_png_lock;
    int is_done;
    sem_t num_awake_threads;
} sharedVariables;

// provided in starter code.
typedef struct recv_buf2 {
    char *buf;       /* memory to hold a copy of received data */
    size_t size;     /* size of valid data in buf in bytes*/
    size_t max_size; /* max capacity of buf in bytes*/
    int seq;         /* >=0 sequence number extracted from http header */
                     /* <0 indicates an invalid seq number */
} RECV_BUF;

ARGS arguments;
sharedVariables shared_thread_variables;
pthread_t* p_tids;

int processInput(ARGS* arguments, char* argv[], int argc);
void initThreadStuff();
void* threadFunction(void* args);
int is_png(char* buf);
int find_http(char *buf, int size, int follow_relative_links, const char *base_url); // Provided
CURL *easy_handle_init(RECV_BUF *ptr, const char *url); // provided in the start code
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata); // Provided
size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata); // Provided
int recv_buf_init(RECV_BUF *ptr, size_t max_size); // Provided
int process_data(CURL *curl_handle, RECV_BUF *p_recv_buf); // Provided
int process_html(CURL *curl_handle, RECV_BUF *p_recv_buf); // Provided
int process_png(CURL *curl_handle, RECV_BUF *p_recv_buf); // Provided
void insertHashTableEntry(char *entry);
void cleanup();
xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlChar *xpath); // Provided
htmlDocPtr mem_getdoc(char *buf, int size, const char *url); // Provided

struct hsearch_data htab;

int main(int argc, char* argv[]){
    processInput(&arguments, argv, argc);

    p_tids = malloc(sizeof(pthread_t) * arguments.numThreads);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    xmlInitParser();

    double times[2];
    struct timeval tv;

    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }
    times[0] = (tv.tv_sec) + tv.tv_usec/1000000.;

    // Initalize shared thread variables.
    initThreadStuff();
    
    for (int i = 0; i < arguments.numThreads; ++i){
        pthread_create(p_tids + i, NULL, &threadFunction, (void*)&i);
    }

    // Join threads
    for (int i = 0; i < arguments.numThreads; ++i){
        pthread_join(p_tids[i], NULL);
    }

    print_queue(&shared_thread_variables.png_urls, "png_urls.txt");
    if (arguments.logFile != NULL){
        print_queue(&shared_thread_variables.all_urls, arguments.logFile);
    }

    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }
    times[1] = (tv.tv_sec) + tv.tv_usec/1000000.;
    printf("findpng2 execution time: %.6lf seconds\n",  times[1] - times[0]);

    free(p_tids);
    cleanup();
    return 0;
}

unsigned int get_count() { 
    unsigned int num_active_threads;
    pthread_mutex_lock(&shared_thread_variables.queue_lock);
    num_active_threads = shared_thread_variables.num_active_threads;
    pthread_mutex_unlock(&shared_thread_variables.queue_lock);
    return num_active_threads;
}

void increment() { 
    pthread_mutex_lock(&shared_thread_variables.queue_lock);
    shared_thread_variables.num_active_threads++;
    pthread_mutex_unlock(&shared_thread_variables.queue_lock);
}

void decrement() { 
    pthread_mutex_lock(&shared_thread_variables.queue_lock);
    shared_thread_variables.num_active_threads--;
    pthread_mutex_unlock(&shared_thread_variables.queue_lock);
}

// First thread to enter should block all other threads till it pushes new urls to the frontier.
// Once URLS are pushed, the thread doing the pushing should signal all other threads to awaken via pthread_cond
// Upon awakening, each thread should first check if we have retrieved all required unique images. Then, it should
void* threadFunction(void* args){
    RECV_BUF recv_buf;
    CURL* curl_handle = easy_handle_init(&recv_buf, arguments.startingURL);
    if ( curl_handle == NULL ) {
        printf("Curl initialization failed. Exiting...\n");
        curl_global_cleanup();
        abort();
    }

    CURLcode res;
    char* popped_url;
    // printf("Thread ID: %lu\n", tid);
    unsigned int png_urls_queue_size;
    unsigned int frontier_queue_size;

    while (1){
        if ( recv_buf_init(&recv_buf, BUF_SIZE) != 0 ) {
            return NULL;
        }
        // *** This shouldn't be required ***
        if (shared_thread_variables.is_done) {
            free(recv_buf.buf);
            curl_easy_cleanup(curl_handle);
            pthread_exit(NULL);
        }
        png_urls_queue_size = get_queueSize(&shared_thread_variables.png_urls);
        frontier_queue_size = get_queueSize(&shared_thread_variables.frontier);

        // If every thread other than the current one is asleep or the number of unqiue PNG URLs found is equal to the max specified by the user
        decrement();
        if (((get_count() == 0) && (frontier_queue_size == 0)) || (png_urls_queue_size == arguments.numUniqueURLs)){
            free(recv_buf.buf);
            shared_thread_variables.is_done = 1;
            pthread_cond_broadcast(&shared_thread_variables.cond_variable);
            curl_easy_cleanup(curl_handle);
            pthread_exit(NULL);
        }
        
        pthread_mutex_lock(&shared_thread_variables.process_data_lock);

        // thread 1 layout for standard pthread_cond_wait/pthread_cond_signal pattern
        // pthread_mutex_lock(&mutex);
        // while (!condition)
        // {pthread_cond_wait(&cond, &mutex);}
        // /* do something that requires holding the mutex and condition is true */
        // pthread_mutex_unlock(&mutex);
        pthread_mutex_lock(&shared_thread_variables.cond_lock);

        png_urls_queue_size = get_queueSize(&shared_thread_variables.png_urls);

        // Check if while the current thread was asleep if any or all of the conditions to end the program were satifisied
        if (shared_thread_variables.is_done || (png_urls_queue_size == arguments.numUniqueURLs)) { 
            free(recv_buf.buf);
            pthread_mutex_unlock(&shared_thread_variables.cond_lock);
            pthread_mutex_unlock(&shared_thread_variables.process_data_lock);
            curl_easy_cleanup(curl_handle);
            pthread_exit(NULL);
        }

        // If the frontier queue is empty the current thread should sleep until a different thread signals that there is a 
        // new item in the frontier queue to process
        if (is_empty(&shared_thread_variables.frontier)) {
             // sleep until signaled
            pthread_cond_wait(&shared_thread_variables.cond_variable, &shared_thread_variables.cond_lock);
            
            png_urls_queue_size = get_queueSize(&shared_thread_variables.png_urls);

            // Check if while the current thread was asleep if any or all of the conditions to end the program were satifisied
            if (shared_thread_variables.is_done || (png_urls_queue_size == arguments.numUniqueURLs)) {
                free(recv_buf.buf);
                pthread_mutex_unlock(&shared_thread_variables.cond_lock);
                pthread_mutex_unlock(&shared_thread_variables.process_data_lock);
                curl_easy_cleanup(curl_handle);
                pthread_exit(NULL);
            }

        }
        increment();
        // Pop from queue and update curl URL
        popped_url = pop_front(&shared_thread_variables.frontier);

        // *** This shouldn't be required ***
        if (popped_url == NULL){
            free(recv_buf.buf);
            curl_easy_cleanup(curl_handle);
            pthread_mutex_unlock(&shared_thread_variables.cond_lock);
            pthread_mutex_unlock(&shared_thread_variables.process_data_lock);
            pthread_exit(NULL);
        }

        // printf("Thread: %ld is here with %s\n", tid, popped_url);
        // Add to hash table to keep track of visited URLS
        push_back(&shared_thread_variables.all_urls, popped_url);
        pthread_mutex_unlock(&shared_thread_variables.cond_lock);
        pthread_mutex_unlock(&shared_thread_variables.process_data_lock);
       
        curl_easy_setopt(curl_handle, CURLOPT_URL, popped_url);

        res = curl_easy_perform(curl_handle);
        for (int i = 0; i < MAX_NUM_ATTEMPTS-1 && (res != CURLE_OK); ++i){
            res = curl_easy_perform(curl_handle);
        }

        if (res == CURLE_OK){
            process_data(curl_handle, &recv_buf);
        }

        free(recv_buf.buf);
    }

    // print_queue(&shared_thread_variables.frontier);
    
    curl_easy_cleanup(curl_handle);
    return NULL;
}

// Inits RECV_BUF too
CURL *easy_handle_init(RECV_BUF *ptr, const char *url)
{
    CURL *curl_handle = NULL;

    if ( ptr == NULL || url == NULL) {
        return NULL;
    }

    /* init a curl session */
    curl_handle = curl_easy_init();

    if (curl_handle == NULL) {
        fprintf(stderr, "curl_easy_init: returned NULL\n");
        return NULL;
    }

    /* specify URL to get */
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);

    /* register write call back function to process received data */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl3); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)ptr);

    /* register header call back function to process received header data */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)ptr);

    /* some servers requires a user-agent field */
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "ece252 lab4 crawler");

    /* follow HTTP 3XX redirects */
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    /* continue to send authentication credentials when following locations */
    curl_easy_setopt(curl_handle, CURLOPT_UNRESTRICTED_AUTH, 1L);
    /* max numbre of redirects to follow sets to 5 */
    curl_easy_setopt(curl_handle, CURLOPT_MAXREDIRS, 5L);
    /* supports all built-in encodings */ 
    curl_easy_setopt(curl_handle, CURLOPT_ACCEPT_ENCODING, "");
    /* Enable the cookie engine without reading any initial cookies */
    curl_easy_setopt(curl_handle, CURLOPT_COOKIEFILE, "");
    /* allow whatever auth the proxy speaks */
    curl_easy_setopt(curl_handle, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
    /* allow whatever auth the server speaks */
    curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_ANY);

    return curl_handle;
}

size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata)
{
    int realsize = size * nmemb;
    RECV_BUF *p = userdata;

    if (realsize > strlen(ECE252_HEADER) &&
	strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0) {

        /* extract img sequence number */
	p->seq = atoi(p_recv + strlen(ECE252_HEADER));

    }
    return realsize;
}

size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata)
{
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;
 
    if (p->size + realsize + 1 > p->max_size) {/* hope this rarely happens */ 
        /* received data is not 0 terminated, add one byte for terminating 0 */
        size_t new_size = p->max_size + max(BUF_INC, realsize + 1);   
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
    ptr->seq = -1;              /* valid seq should be positive */
    return 0;
}

void initThreadStuff(){
    int status = hcreate_r(500, &htab);
    if (status == 0){
        puts("Failed to initalize hash table. Exiting...");
        exit(1);
    }

    shared_thread_variables.num_active_threads = arguments.numThreads;

    // Since pushing to queue, we cannot "free()" the passed in arg'd, so we must copy its content to a malloc'd variable
    // so the queue can properly free everything.
    char* temp = arguments.startingURL;
    arguments.startingURL = malloc(sizeof(char) * strlen(temp) + 1);
    memcpy(arguments.startingURL, temp, strlen(temp) + 1);

    init_queue(&shared_thread_variables.frontier);
    init_queue(&shared_thread_variables.png_urls);
    init_queue(&shared_thread_variables.all_urls);
    push_back(&shared_thread_variables.frontier, arguments.startingURL);
    insertHashTableEntry(arguments.startingURL);
    pthread_cond_init(&shared_thread_variables.cond_variable, NULL);

    if (pthread_mutex_init(&shared_thread_variables.hash_lock, NULL) != 0) {
        printf("\n hash_lock mutex init has failed\n");
        exit(0);
    }

    if (pthread_mutex_init(&shared_thread_variables.queue_lock, NULL) != 0) {
        printf("\n queue_lock mutex init has failed\n");
        exit(0);
    }

    if (pthread_mutex_init(&shared_thread_variables.cond_lock, NULL) != 0) {
        printf("\n cond_lock mutex init has failed\n");
        exit(0);
    }

    if (pthread_mutex_init(&shared_thread_variables.process_data_lock, NULL) != 0) {
        printf("\n process_data_lock mutex init has failed\n");
        exit(0);
    }

    if (pthread_mutex_init(&shared_thread_variables.process_png_lock, NULL) != 0) {
        printf("\n process_png_lock mutex init has failed\n");
        exit(0);
    }

    shared_thread_variables.is_done = 0;
    sem_init(&shared_thread_variables.num_awake_threads, 1, arguments.numThreads);
}

void insertHashTableEntry(char *entry) { 
    if (entry == NULL){
        printf("Invalid Data to push into hash table.");
        return;
    }
    // Inserting seed url as the first webpage (NOT VISITED YET).
    shared_thread_variables.e.key = entry;
    shared_thread_variables.e.data = (void *) 0;
    hsearch_r(shared_thread_variables.e, ENTER, &shared_thread_variables.ep, &htab);
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

// Returns 1 if its a png, otherwise it will return 0.
int is_png(char* buf) {
    if (buf == NULL){
        return 0;
    }
    unsigned char cmpHeader[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 8; ++i){
        if ( ((unsigned char*) buf)[i] != cmpHeader[i] ){
            return 0;
        }
    }
    return 1;
}

/**
 * @brief process teh download data by curl. Extracts content type (aka png, or html) from curl'd url and processes it based on it.
 * @param CURL *curl_handle is the curl handler
 * @param RECV_BUF p_recv_buf contains the received data. 
 * @return 0 on success; non-zero otherwise
 */
int process_data(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    long response_code;

    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
    if ( response_code >= 400 ) { 
    	// fprintf(stderr, "Error.\n");
        return 1;
    }

    char *ct = NULL;
    curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_TYPE, &ct);
    if (ct != NULL ) {
    	// printf("Content-Type: %s, len=%ld\n", ct, strlen(ct));
    }

    if ( strstr(ct, CT_HTML) ) {
        return process_html(curl_handle, p_recv_buf);
    } else if ( strstr(ct, CT_PNG) ) {
        return process_png(curl_handle, p_recv_buf);
    }

    // Push url to png url queue.
    return 0;
}

int find_http(char *buf, int size, int follow_relative_links, const char *base_url)
{
    int i;
    htmlDocPtr doc;
    xmlChar *xpath = (xmlChar*) "//a/@href";
    xmlNodeSetPtr nodeset;
    xmlXPathObjectPtr result;
    xmlChar *href;
		
    if (buf == NULL) {
        return 1;
    }

    doc = mem_getdoc(buf, size, base_url);
    result = getnodeset (doc, xpath);
    if (result) {
        nodeset = result->nodesetval;
        for (i=0; i < nodeset->nodeNr; i++) {
            href = xmlNodeListGetString(doc, nodeset->nodeTab[i]->xmlChildrenNode, 1);
            if ( follow_relative_links ) {
                xmlChar *old = href;
                href = xmlBuildURI(href, (xmlChar *) base_url);
                xmlFree(old);
            }
            if ( href != NULL && !strncmp((const char *)href, "http", 4) ) {
                char* urlToSave = malloc(sizeof(char) * 256);
                strncpy(urlToSave, (char*) href, 256);

                pthread_mutex_lock(&shared_thread_variables.cond_lock);
                shared_thread_variables.e.key = urlToSave;
                hsearch_r(shared_thread_variables.e, FIND, &shared_thread_variables.ep, &htab);
                if (shared_thread_variables.ep == NULL){ // Does not exist, so push URL to frontier
                    // thread 2 layout for standard pthread_cond_wait/pthread_cond_signal pattern
                    // pthread_mutex_lock(&mutex);
                    // /* do something that might make condition true */
                    // pthread_cond_signal(&cond);
                    // pthread_mutex_unlock(&mutex);
                    insertHashTableEntry(urlToSave);
                    push_back(&shared_thread_variables.frontier, urlToSave);
                    // Signal that there is a new item in the frontier queue to process
                    pthread_cond_signal(&shared_thread_variables.cond_variable);
                } else {
                    free(urlToSave);
                }
                pthread_mutex_unlock(&shared_thread_variables.cond_lock);
            }
            xmlFree(href);
        }
        xmlXPathFreeObject (result);
    }
    xmlFreeDoc(doc);
    return 0;
}

int process_html(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    int follow_relative_link = 1;
    char *url = NULL; 

    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &url);
    find_http(p_recv_buf->buf, p_recv_buf->size, follow_relative_link, url); 
    return 0;
}

int process_png(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    char *eurl = NULL;          /* effective URL */
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eurl);
    if ( eurl != NULL) {
        // First verify if it is an actual image before pushing to queue.
        if (!is_png(p_recv_buf->buf)){
            return 0;
        }
        char* urlToSave = malloc(sizeof(char) * 256);
        strncpy(urlToSave, eurl, 256);

        // printf("The PNG url is: %s\n", urlToSave);
        pthread_mutex_lock(&shared_thread_variables.process_png_lock);
        if (get_queueSize(&shared_thread_variables.png_urls) == arguments.numUniqueURLs){
            free(urlToSave);
            pthread_mutex_unlock(&shared_thread_variables.process_png_lock);
            return 0;
        }
        push_back(&shared_thread_variables.png_urls, urlToSave);
        pthread_mutex_unlock(&shared_thread_variables.process_png_lock);
    }    
    return 0;
}

xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlChar *xpath)
{
	
    xmlXPathContextPtr context;
    xmlXPathObjectPtr result;

    context = xmlXPathNewContext(doc);
    if (context == NULL) {
        printf("Error in xmlXPathNewContext\n");
        return NULL;
    }
    result = xmlXPathEvalExpression(xpath, context);
    xmlXPathFreeContext(context);
    if (result == NULL) {
        printf("Error in xmlXPathEvalExpression\n");
        return NULL;
    }
    if(xmlXPathNodeSetIsEmpty(result->nodesetval)){
        xmlXPathFreeObject(result);
        // printf("No result\n");
        return NULL;
    }
    return result;
}

htmlDocPtr mem_getdoc(char *buf, int size, const char *url)
{
    int opts = HTML_PARSE_NOBLANKS | HTML_PARSE_NOERROR | \
               HTML_PARSE_NOWARNING | HTML_PARSE_NONET;
    htmlDocPtr doc = htmlReadMemory(buf, size, url, NULL, opts);
    
    if ( doc == NULL ) {
        // fprintf(stderr, "Document not parsed successfully.\n");
        return NULL;
    }
    return doc;
}

void cleanup(){
    xmlCleanupParser();
    curl_global_cleanup();

    clean_queue(&shared_thread_variables.frontier);
    clean_queue(&shared_thread_variables.png_urls);
    clean_queue(&shared_thread_variables.all_urls);

    hdestroy_r(&htab);

    pthread_mutex_destroy(&shared_thread_variables.cond_lock);
    pthread_mutex_destroy(&shared_thread_variables.queue_lock);
    pthread_mutex_destroy(&shared_thread_variables.hash_lock);
    pthread_mutex_destroy(&shared_thread_variables.process_data_lock);
    pthread_mutex_destroy(&shared_thread_variables.process_png_lock);

    sem_destroy(&shared_thread_variables.num_awake_threads);
}