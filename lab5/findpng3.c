#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <search.h> // Library used to include hash table.
#include <curl/curl.h>
#include <curl/multi.h>
#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/uri.h>
#include "queue.h"

#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 1048576 /* 1024*1024 = 1M */
#define BUF_INC 524288   /* 1024*512  = 0.5M */
#define MAX_WAIT_MSECS 30*1000 /* Wait max. 30 seconds */

#define CT_PNG "image/png"
#define CT_HTML "text/html"
#define CT_PNG_LEN 9
#define CT_HTML_LEN 9
#define CNT 4

#define max(a, b) \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

typedef struct _args
{
    int maxNumConcurrentConnections;
    int numUniqueURLs;
    char *logFile;
    char *startingURL;
} ARGS;

typedef struct recv_buf2
{
    char *buf;       /* memory to hold a copy of received data */
    size_t size;     /* size of valid data in buf in bytes*/
    size_t max_size; /* max capacity of buf in bytes*/
    int seq;         /* >=0 sequence number extracted from http header */
                     /* <0 indicates an invalid seq number */
    int curl_id;
} RECV_BUF;

// Utilize a hashmap to store already visited webpages to prevent visiting the same webpage more than once.
typedef struct _sharedVariables
{
    ENTRY e, *ep;      // Hash table of every site seen. Keeps track of visited or not visited
    QUEUE frontier;    // To-visit urls
    QUEUE png_urls;    // URLS that are pngs.
    QUEUE visted_urls; // A queue used to store all URLS for deallocation.
    RECV_BUF **recv_buffers;
    CURLM *cm;
    CURL** curl_handlers;
    int num_of_eh;
} sharedVariables;

ARGS arguments;
sharedVariables shared_thread_variables;
pthread_t *p_tids;

int processInput(ARGS *arguments, char *argv[], int argc);
void init_stuff();
void crawl();
int is_png(char *buf);
int find_http(char *buf, int size, int follow_relative_links, const char *base_url); // Provided
CURL* easy_handle_init(RECV_BUF *ptr, char* startingURL);
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata);   // Provided
size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata); // Provided
int recv_buf_init(RECV_BUF *ptr, size_t max_size);                                // Provided
int process_data(CURL *curl_handle, RECV_BUF *p_recv_buf);                        // Provided
int process_html(CURL *curl_handle, RECV_BUF *p_recv_buf);                        // Provided
int process_png(CURL *curl_handle, RECV_BUF *p_recv_buf);                         // Provided
void insertHashTableEntry(char *entry);
void cleanup();
xmlXPathObjectPtr getnodeset(xmlDocPtr doc, xmlChar *xpath); // Provided
htmlDocPtr mem_getdoc(char *buf, int size, const char *url); // Provided
void read_messages();
void update_curl_handle(CURL* eh, RECV_BUF* recv_buf);
void reset_recv_buf(RECV_BUF* recv_buf);
void setup_curl_multi();
void reset_all_ehs();

struct hsearch_data htab;

// First perform a single curl operation to fetch all urls from starting seed url. Then perform multi curl.
int main(int argc, char *argv[])
{
    if (!processInput(&arguments, argv, argc)){
        return 0;
    }

    shared_thread_variables.cm = NULL;

    curl_global_init(CURL_GLOBAL_DEFAULT);

    shared_thread_variables.cm = curl_multi_init();

    init_stuff();

    setup_curl_multi();

    // Perform inital crawl of seed url. Then distribute url to other crawlers.
    RECV_BUF temp_recv_buffer;
    char* popped_url = pop_front(&shared_thread_variables.frontier);
    push_back(&shared_thread_variables.visted_urls, popped_url);
    recv_buf_init(&temp_recv_buffer, BUF_SIZE); 
    CURL* starting_curl = easy_handle_init(&temp_recv_buffer, popped_url);
    curl_easy_perform(starting_curl);
    process_data(starting_curl, &temp_recv_buffer);
    free(temp_recv_buffer.buf);
    curl_easy_cleanup(starting_curl);

    for (int i = 0; !is_empty(&shared_thread_variables.frontier) && i < arguments.maxNumConcurrentConnections; i++){
        char* popped_url = pop_front(&shared_thread_variables.frontier);
        push_back(&shared_thread_variables.visted_urls, popped_url);
        CURL* curr_eh = shared_thread_variables.curl_handlers[i];
        curl_easy_setopt(curr_eh, CURLOPT_URL, popped_url);
        curl_multi_add_handle(shared_thread_variables.cm, curr_eh);
    }

    crawl();

    print_queue(&shared_thread_variables.png_urls, "png_urls.txt");
    if (arguments.logFile != NULL)
    {
        print_queue(&shared_thread_variables.visted_urls, arguments.logFile);
    }

    cleanup();
    return 0;
}

void crawl()
{
    /*
    1) Reuse curl_handlers. Must remove from multi, and re-add. Upon re-adding ensure to update url
    2) Keep performing multi-perform till either the frontier queue is empty, or we have reached m
    3) If frontier is empty, then remove the curl handler completely. Moment the frontier is non-empty,
        create x amount of curl_handlers till frontier is empty again OR once x == -t argument
    */
    int still_running = 0;

    while (1){
        do
        {   
            if (get_queueSize(&shared_thread_variables.png_urls) == arguments.numUniqueURLs) {
                return;
            }

            curl_multi_perform(shared_thread_variables.cm, &still_running);

            int numfds = 0;
            int res = curl_multi_wait(shared_thread_variables.cm, NULL, 0, MAX_WAIT_MSECS, &numfds);
            if (res != CURLM_OK)
            {
                fprintf(stderr, "error: curl_multi_wait() returned %d\n", res);
                return;
            }
            else
            {   
                read_messages();
            }
            // printf("size of frontier: %d, size of png_queue: %d, and still running: %d\n", get_queueSize(&shared_thread_variables.frontier), get_queueSize(&shared_thread_variables.png_urls), still_running);
        } while (still_running);

        if (is_empty(&shared_thread_variables.frontier)){
            break;
        }

        reset_all_ehs();
    }
}

void read_messages()
{
    CURLcode return_code = 0;
    // char *popped_url;
    CURLMsg* msg;
    int msgs_left;
    CURL *eh = NULL;
    RECV_BUF *recv_buf = NULL;

    while ((msg = curl_multi_info_read(shared_thread_variables.cm, &msgs_left)))
    {
        if (msg->msg == CURLMSG_DONE)
        {
            eh = msg->easy_handle;
            curl_easy_getinfo(eh, CURLINFO_PRIVATE, &recv_buf);

            return_code = msg->data.result;
            if (return_code != CURLE_OK)
            {
                reset_recv_buf(recv_buf);
                curl_multi_remove_handle(shared_thread_variables.cm, shared_thread_variables.curl_handlers[recv_buf->curl_id]);
                continue;
            }

            process_data(eh, recv_buf);
            reset_recv_buf(recv_buf);
            curl_multi_remove_handle(shared_thread_variables.cm, shared_thread_variables.curl_handlers[recv_buf->curl_id]);
        }
        else
        {
            fprintf(stderr, "error: after curl_multi_info_read(), CURLMsg=%d\n", msg->msg);
            curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &recv_buf);
            curl_multi_remove_handle(shared_thread_variables.cm, shared_thread_variables.curl_handlers[recv_buf->curl_id]);
        }
    }
}

// Sets buffer to 0 and resets size and seq
void reset_recv_buf(RECV_BUF* recv_buf){
    memset(recv_buf->buf, 0, recv_buf->size);
    recv_buf->size = 0;
    recv_buf->seq = -1;
}

void reset_all_ehs(){
    for (int i = 0; !is_empty(&shared_thread_variables.frontier) && i < arguments.maxNumConcurrentConnections; i++){
        char* popped_url = pop_front(&shared_thread_variables.frontier);
        push_back(&shared_thread_variables.visted_urls, popped_url);
        RECV_BUF* curr_recv_buf = shared_thread_variables.recv_buffers[i];
        CURL* curr_eh = shared_thread_variables.curl_handlers[i];
        reset_recv_buf(curr_recv_buf);
        curl_easy_setopt(curr_eh, CURLOPT_URL, popped_url);
        curl_multi_add_handle(shared_thread_variables.cm, curr_eh);
        shared_thread_variables.num_of_eh++;
    }   
}

void setup_curl_multi(){
    for (int i = 0; i < arguments.maxNumConcurrentConnections; i++)
    {
        RECV_BUF *recv_buf = malloc(sizeof(RECV_BUF));
        recv_buf_init(recv_buf, BUF_SIZE);
        shared_thread_variables.recv_buffers[i] = recv_buf;
        CURL* eh = easy_handle_init(recv_buf, NULL);
        shared_thread_variables.curl_handlers[i] = eh;
        shared_thread_variables.recv_buffers[i]->curl_id = i;
    }
}

CURL* easy_handle_init(RECV_BUF *ptr, char* startingURL)
{
    CURL *curl_handle = NULL;

    if (ptr == NULL)
    {
        return NULL;
    }

    /* init a curl session */
    curl_handle = curl_easy_init();

    if (curl_handle == NULL)
    {
        fprintf(stderr, "curl_easy_init: returned NULL\n");
        return NULL;
    }

    /* specify URL to get */
    if (startingURL == NULL){
        curl_easy_setopt(curl_handle, CURLOPT_URL, arguments.startingURL);
    } else {
        curl_easy_setopt(curl_handle, CURLOPT_URL, startingURL);
    }

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

    curl_easy_setopt(curl_handle, CURLOPT_PRIVATE, (void *)ptr);

    return curl_handle;
}

size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata)
{;
    int realsize = size * nmemb;
    RECV_BUF *p = userdata;

    if (realsize > strlen(ECE252_HEADER) &&
        strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0)
    {

        /* extract img sequence number */
        p->seq = atoi(p_recv + strlen(ECE252_HEADER));
    }
    return realsize;
}

size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata)
{
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;

    if (p->size + realsize + 1 > p->max_size)
    { /* hope this rarely happens */
        /* received data is not 0 terminated, add one byte for terminating 0 */
        size_t new_size = p->max_size + max(BUF_INC, realsize + 1);
        char *q = realloc(p->buf, new_size);
        if (q == NULL)
        {
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

    if (ptr == NULL)
    {
        return 1;
    }

    p = malloc(max_size);
    if (p == NULL)
    {
        return 2;
    }

    ptr->buf = p;
    ptr->size = 0;
    ptr->max_size = max_size;
    ptr->seq = -1; /* valid seq should be positive */
    ptr->curl_id = -1; // Valid id should be positive
    return 0;
}

void init_stuff()
{
    int status = hcreate_r(500, &htab);
    if (status == 0)
    {
        puts("Failed to initalize hash table. Exiting...");
        exit(1);
    }

    char* starting_url = malloc(sizeof(char) * 256);
    strncpy(starting_url, arguments.startingURL, 256);

    shared_thread_variables.recv_buffers = malloc(sizeof(RECV_BUF *) * arguments.maxNumConcurrentConnections);
    shared_thread_variables.curl_handlers = malloc(sizeof(CURL *) * arguments.maxNumConcurrentConnections);

    // init_queue(&shared_thread_variables.free_buffers);
    init_queue(&shared_thread_variables.frontier);
    init_queue(&shared_thread_variables.visted_urls);
    init_queue(&shared_thread_variables.png_urls);

    insertHashTableEntry(starting_url);
    push_back(&shared_thread_variables.frontier, starting_url);
}

void insertHashTableEntry(char *entry)
{
    if (entry == NULL)
    {
        printf("Invalid Data to push into hash table.");
        return;
    }
    // Inserting seed url as the first webpage (NOT VISITED YET).
    shared_thread_variables.e.key = entry;
    shared_thread_variables.e.data = (void *)0;
    hsearch_r(shared_thread_variables.e, ENTER, &shared_thread_variables.ep, &htab);
    if (shared_thread_variables.ep == NULL)
    { // Failed to insert for whatever reason.
        fprintf(stderr, "entry failed\n");
        exit(EXIT_FAILURE);
    }
}

int processInput(ARGS *arguments, char *argv[], int argc)
{
    int c;
    arguments->numUniqueURLs = 50;
    arguments->maxNumConcurrentConnections = 1;
    char *str = "option requires an argument";

    arguments->startingURL = argv[argc - 1];

    while ((c = getopt(argc, argv, "t:m:v:")) != -1)
    {
        switch (c)
        {
        case 't':
            arguments->maxNumConcurrentConnections = atoi(optarg);
            if (arguments->maxNumConcurrentConnections <= 0)
            {
                fprintf(stderr, "%s: %s > 0 -- 't'\n", argv[0], str);
                return -1;
            }
            break;
        case 'm':
            arguments->numUniqueURLs = atoi(optarg);
            if (arguments->numUniqueURLs < 0)
            {
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
int is_png(char *buf)
{
    if (buf == NULL)
    {
        return 0;
    }
    unsigned char cmpHeader[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (memcmp(cmpHeader, buf, 8))
    {
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
    if (response_code >= 400)
    {
        // fprintf(stderr, "Error.\n");
        return 1;
    }

    char *ct = NULL;
    curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_TYPE, &ct);
    if (ct != NULL)
    {
        // printf("Content-Type: %s, len=%ld\n", ct, strlen(ct));
    }

    if (strstr(ct, CT_HTML))
    {
        return process_html(curl_handle, p_recv_buf);
    }
    else if (strstr(ct, CT_PNG))
    {
        return process_png(curl_handle, p_recv_buf);
    }

    // Push url to png url queue.
    return 0;
}

int find_http(char *buf, int size, int follow_relative_links, const char *base_url)
{

    int i;
    htmlDocPtr doc;
    xmlChar *xpath = (xmlChar *)"//a/@href";
    xmlNodeSetPtr nodeset;
    xmlXPathObjectPtr result;
    xmlChar *href;

    if (buf == NULL)
    {
        return 1;
    }

    doc = mem_getdoc(buf, size, base_url);
    result = getnodeset(doc, xpath);
    if (result)
    {
        nodeset = result->nodesetval;
        for (i = 0; i < nodeset->nodeNr; i++)
        {
            href = xmlNodeListGetString(doc, nodeset->nodeTab[i]->xmlChildrenNode, 1);
            if (follow_relative_links)
            {
                xmlChar *old = href;
                href = xmlBuildURI(href, (xmlChar *)base_url);
                xmlFree(old);
            }
            if (href != NULL && !strncmp((const char *)href, "http", 4))
            {
                char* urlToSave = malloc(sizeof(char) * 256);
                strncpy(urlToSave, (char*) href, 256);

                shared_thread_variables.e.key = urlToSave;
                hsearch_r(shared_thread_variables.e, FIND, &shared_thread_variables.ep, &htab);
                if (shared_thread_variables.ep == NULL)
                { // Does not exist, so push URL to frontier
                    insertHashTableEntry(urlToSave);
                    push_back(&shared_thread_variables.frontier, urlToSave);
                    // printf("Href: %s\n", href);
                }
                else
                {
                    free(urlToSave);
                }
            }
            xmlFree(href);
        }
        xmlXPathFreeObject(result);
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
    char *eurl = NULL; /* effective URL */
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eurl);
    if (eurl != NULL)
    {
        if (get_queueSize(&shared_thread_variables.png_urls) == arguments.numUniqueURLs) {
            return 0;
        }

        // First verify if it is an actual image before pushing to queue.
        if (!is_png(p_recv_buf->buf))
        {
            return 0;
        }
        char* urlToSave = malloc(sizeof(char) * 256);
        strncpy(urlToSave, (char*) eurl, 256);

        // printf("The PNG url is: %s\n", urlToSave);
        push_back(&shared_thread_variables.png_urls, urlToSave);
    }
    return 0;
}

xmlXPathObjectPtr getnodeset(xmlDocPtr doc, xmlChar *xpath)
{

    xmlXPathContextPtr context;
    xmlXPathObjectPtr result;

    context = xmlXPathNewContext(doc);
    if (context == NULL)
    {
        printf("Error in xmlXPathNewContext\n");
        return NULL;
    }
    result = xmlXPathEvalExpression(xpath, context);
    xmlXPathFreeContext(context);
    if (result == NULL)
    {
        printf("Error in xmlXPathEvalExpression\n");
        return NULL;
    }
    if (xmlXPathNodeSetIsEmpty(result->nodesetval))
    {
        xmlXPathFreeObject(result);
        // printf("No result\n");
        return NULL;
    }
    return result;
}

htmlDocPtr mem_getdoc(char *buf, int size, const char *url)
{
    int opts = HTML_PARSE_NOBLANKS | HTML_PARSE_NOERROR |
               HTML_PARSE_NOWARNING | HTML_PARSE_NONET;
    htmlDocPtr doc = htmlReadMemory(buf, size, url, NULL, opts);

    if (doc == NULL)
    {
        fprintf(stderr, "Document not parsed successfully.\n");
        return NULL;
    }
    return doc;
}

void cleanup()
{
    clean_queue(&shared_thread_variables.frontier);
    clean_queue(&shared_thread_variables.png_urls);
    clean_queue(&shared_thread_variables.visted_urls);

    hdestroy_r(&htab);

    for (int i = 0; i < arguments.maxNumConcurrentConnections; i++)
    {
        free(shared_thread_variables.recv_buffers[i]->buf);
        free(shared_thread_variables.recv_buffers[i]);
        curl_easy_cleanup(shared_thread_variables.curl_handlers[i]);
    }

    free(shared_thread_variables.curl_handlers);
    free(shared_thread_variables.recv_buffers);
    curl_multi_cleanup(shared_thread_variables.cm);
    curl_global_cleanup();
}