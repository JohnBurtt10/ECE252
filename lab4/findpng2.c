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
    QUEUE visted_urls; // A queue used to store all URLS for deallocation.
    volatile int num_active_threads; // Keep tracking of number of threads actively crawling. // WONT BE NEEDED
    pthread_cond_t cond_variable;
    pthread_mutex_t queue_lock;
    pthread_mutex_t hash_lock;
    pthread_mutex_t cond_lock;
    pthread_mutex_t process_data_lock;
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
    print_queue(&shared_thread_variables.visted_urls, arguments.logFile);

    free(p_tids);
    cleanup();
    return 0;
}

// void* threadFunction(void* args){
//     RECV_BUF* recv_buf;
//     recv_buf_init(recv_buf, BUF_SIZE);
//     while (1) {
//         // If n-1 threads are sleeping, set a global flag "done" and terminate everything.
//         if (n-1 threads are sleeping){
//             set global bool to true
//             broad_cast_all on cond variable
//             terminate current thread
//         }
//         pthread_mutex_lock(&shared_thread_variables.cond_lock); 
//         // sleep until signaled
//         if (is_empty(&shared_thread_variables.frontier)) { 
//             pthread_cond_wait(&shared_thread_variables.cond_variable, &shared_thread_variables.cond_lock);
//             if (consumed > argument.pngcount || allPNGSFound)

//             // If global bool "done" true, terminate thread.
//         }
//         char* url = pop_front(&shared_thread_variables.frontier);
//         CURL* curl_handle = easy_handle_init(recv_buf, url);
        


//         curl_easy_cleanup(curl_handle);
//         break;
//     }    
//     return NULL;
// }

// First thread to enter should block all other threads till it pushes new urls to the frontier.
// Once URLS are pushed, the thread doing the pushing should signal all other threads to awaken via pthread_cond
// Upon awakening, each thread should first check if we have retrieved all required unique images. Then, it should
void* threadFunction(void* args){
    RECV_BUF recv_buf;
    CURL* curl_handle = easy_handle_init(&recv_buf, arguments.startingURL);
    CURLcode res;
    char* popped_url;
    pthread_t tid = pthread_self();
    printf("Thread ID: %lu\n", tid);
    int queueSize;

    while (1){
        if ( curl_handle == NULL ) {
            printf("Curl initialization failed. Exiting...\n");
            curl_global_cleanup();
            abort();
        }

        if ( recv_buf_init(&recv_buf, BUF_SIZE) != 0 ) {
            return NULL;
        }   

        queueSize = get_queueSize(&shared_thread_variables.png_urls);
        if (queueSize == arguments.numUniqueURLs){
            free(recv_buf.buf);
            break;
        }
        queueSize = get_queueSize(&shared_thread_variables.frontier);
        // If n-1 threads are sleeping, set a global flag "done" and terminate everything.
        if (queueSize == 0 && sem_trywait(&shared_thread_variables.num_awake_threads)){
            // printf("thread: %ld is broadcasting!!!!\n", tid);
            free(recv_buf.buf);
            shared_thread_variables.is_done = 1;
            pthread_cond_broadcast(&shared_thread_variables.cond_variable);
            break;
        }
        pthread_mutex_lock(&shared_thread_variables.cond_lock); 
        // sleep until signaled
        if (is_empty(&shared_thread_variables.frontier)) {
            pthread_cond_wait(&shared_thread_variables.cond_variable, &shared_thread_variables.cond_lock);
            printf("thread: %ld recived signal!\n", tid);
            queueSize = get_queueSize(&shared_thread_variables.png_urls);
            if (queueSize == arguments.numUniqueURLs || shared_thread_variables.is_done) { 
                free(recv_buf.buf);
                printf("thread: %ld is finishing!\n", tid);
                break;
            }
        }  
        pthread_mutex_unlock(&shared_thread_variables.cond_lock);
        sem_post(&shared_thread_variables.num_awake_threads);
        printf("thread: %ld is here\n", tid);
        // Pop from queue and update curl URL
        popped_url = pop_front(&shared_thread_variables.frontier);
        if (popped_url == NULL){
            free(recv_buf.buf);
            break;
        }
        curl_easy_setopt(curl_handle, CURLOPT_URL, popped_url);
        printf("thread: %ld is here with: %s\n", tid, popped_url);

        // Add to hash table to keep track of visited URLS
        insertHashTableEntry(popped_url);
        push_back(&shared_thread_variables.visted_urls, popped_url);

        res = curl_easy_perform(curl_handle);

        if (res == CURLE_OK){
            pthread_mutex_lock(&shared_thread_variables.process_data_lock);
            process_data(curl_handle, &recv_buf);
            pthread_mutex_unlock(&shared_thread_variables.process_data_lock);

            pthread_cond_signal(&shared_thread_variables.cond_variable);
            printf("thread: %ld just signaled!\n", tid);
        } else {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
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
    int status = hcreate_r(1000, &htab);
    if (status == 0){
        puts("Failed to initalize hash table. Exiting...");
        exit(1);
    }

    shared_thread_variables.num_active_threads = 0;

    // Since pushing to queue, we cannot "free()" the passed in arg'd, so we must copy its content to a malloc'd variable
    // so the queue can properly free everything.
    char* temp = arguments.startingURL;
    arguments.startingURL = malloc(sizeof(char) * strlen(temp) + 1);
    memcpy(arguments.startingURL, temp, strlen(temp) + 1);

    init_queue(&shared_thread_variables.frontier);
    init_queue(&shared_thread_variables.png_urls);
    init_queue(&shared_thread_variables.visted_urls);
    push_back(&shared_thread_variables.frontier, arguments.startingURL);
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

    shared_thread_variables.is_done = 0;
    sem_init(&shared_thread_variables.num_awake_threads, 1, arguments.numThreads-1);
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
    if (memcmp(cmpHeader, buf, 8)){
        return 0;
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
    	fprintf(stderr, "Error.\n");
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
                char* urlToSave = malloc(sizeof(unsigned char) * strlen( (char*) href) + 1);
                memcpy(urlToSave, href, strlen((char*) href) + 1);

                shared_thread_variables.e.key = urlToSave;
                hsearch_r(shared_thread_variables.e, FIND, &shared_thread_variables.ep, &htab);
                if (shared_thread_variables.ep == NULL){ // Does not exist, so push URL to frontier
                    push_back(&shared_thread_variables.frontier, urlToSave);
                    // printf("href: %s\n", urlToSave);
                } else {
                    free(urlToSave);
                }
            }
            xmlFree(href);
        }
        xmlXPathFreeObject (result);
    }
    xmlFreeDoc(doc);
    xmlCleanupParser();
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
        char* urlToSave = malloc(sizeof(unsigned char) * strlen( (char*) eurl) + 1);
        memcpy(urlToSave, eurl, strlen((char*) eurl) + 1);
        // printf("The PNG url is: %s\n", urlToSave);
        push_back(&shared_thread_variables.png_urls, urlToSave);
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
        fprintf(stderr, "Document not parsed successfully.\n");
        return NULL;
    }
    return doc;
}

void cleanup(){
    clean_queue(&shared_thread_variables.frontier);
    clean_queue(&shared_thread_variables.png_urls);
    clean_queue(&shared_thread_variables.visted_urls);

    hdestroy_r(&htab);

    curl_global_cleanup();

    pthread_mutex_destroy(&shared_thread_variables.cond_lock);
    pthread_mutex_destroy(&shared_thread_variables.queue_lock);
    pthread_mutex_destroy(&shared_thread_variables.hash_lock);
    pthread_mutex_destroy(&shared_thread_variables.process_data_lock);
}