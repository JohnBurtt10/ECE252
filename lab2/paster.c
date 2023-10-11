#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <curl/curl.h>
#include "../lab1/starter/png_util/zutil.h"    /* for mem_def() and mem_inf() */
#include "../lab1/starter/png_util/lab_png.h"
#include "../lab1/starter/png_util/crc.h"
#include <stdint.h>  // for uint32_t
#include <arpa/inet.h>

#define THREAD_CMD "-t"
#define RANDOM_PNG_SEG "-n"
#define BUF_SIZE 8000  /* 1024*1024 = 1M */
#define BUF_INC  500   /* 1024*512  = 0.5M */
#define ECE252_HEADER "X-Ece252-Fragment: "

#define IDAT_OFFSET_BYTES 33 // 33 bytes from start of file to get IDAT chunk start.

#define IHDR_CHUNK_SIZE 25

#define HEADER_SIZE 8

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
    size_t size_of_png[50];
    volatile bool imageReceived[50];
    volatile int size;
} PNG_BUFFER;

typedef struct data_IHDR *data_IHDR_p;

void processInput(char *command, char *value, args* destination);
void initalizeArguments(args* argument);
// CURL* curlInit(THREAD_ARGS* threadArgs, RECV_BUF* recv_buf);
void* createRequest(void* args);
void initPNGBuffer(PNG_BUFFER* pngBuffer);
int recv_buf_init(RECV_BUF *ptr, size_t max_size);
void get_png_data_IDAT(unsigned char *out, unsigned char *mem_data, long offset, int idat_data_length);
void write_png(unsigned char *IDAT_concat_Data_def, unsigned int total_height, unsigned int pngWidth, size_t IDAT_Concat_Data_Def_Len);
unsigned char* concatenate_png(unsigned char *png_buffer ,unsigned char *array_to_concat, size_t size_of_array_to_concat, size_t *size_of_png_buffer);
void cat_png();
uint32_t get_png_idat_data_length(const uint8_t *memory);
int get_png_data_IHDR_mem(struct data_IHDR *out, char *memory, size_t offset, size_t size);
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
    cat_png();

    for (int i = 0; i < 50; i++){
        free(pngBuffer.pngs[i]);
    }

    curl_global_cleanup();
    return 0;
}

void cat_png() { 
    U64 IDAT_Data_inf_len = 0; /* uncompressed data length */
    size_t IDAT_Concat_Data_Def_Len = 0;

    unsigned int total_height = 0;
    unsigned int pngWidth = 0;

    // FILE *file;

    unsigned char IDAT_array[50];
    unsigned char *png_buffer = NULL;
    unsigned int height; 
    unsigned int width;
    size_t size_of_png_buffer = 0;
    for (size_t i = 1; i < 50; i++){
        data_IHDR_p ihdr = malloc(13);

        height = htonl(400);
        width = htonl(6);
        
        // Now that we have IHDR data, need to retrieve IDAT data. IDAT buffer size = Height * (Width * 4 + 1)
        int IDAT_def_length = height * (width * 4 + 1);
        unsigned char *IDAT_data_inf = malloc(IDAT_def_length);
        unsigned int IDAT_Data_def_len = get_png_idat_data_length(pngBuffer.pngs[i]);
        unsigned char *IDAT_data_def = malloc(IDAT_Data_def_len);

        // SEG FAULTING IN THIS FUNCTION
        get_png_data_IDAT(IDAT_data_def, pngBuffer.pngs[i], IDAT_OFFSET_BYTES+8, IDAT_Data_def_len);

        // // decompressing IDAT data.
        mem_inf(IDAT_data_inf, &IDAT_Data_inf_len, IDAT_data_def, IDAT_Data_def_len);

        png_buffer = concatenate_png(png_buffer, IDAT_data_inf, IDAT_Data_inf_len, &size_of_png_buffer);
        // printf("%lu\n", IDAT_Data_inf_len);

        free(ihdr);
        free(IDAT_data_inf);
        free(IDAT_data_def);
    }

    // Compress the new concancated IDAT data.
    unsigned char *IDAT_Concat_Data_Def = malloc(400*(300 * 4 + 1));
    mem_def(IDAT_Concat_Data_Def, &IDAT_Concat_Data_Def_Len, png_buffer, size_of_png_buffer, Z_DEFAULT_COMPRESSION);
    
    write_png(IDAT_Concat_Data_Def, 400, 300, IDAT_Concat_Data_Def_Len);

    free(IDAT_Concat_Data_Def);
    free(png_buffer);
}

unsigned char* concatenate_png(unsigned char *png_buffer ,unsigned char *array_to_concat, size_t size_of_array_to_concat, size_t *size_of_png_buffer){
    unsigned char *new_buffer = malloc( sizeof(unsigned char) *  (size_of_array_to_concat + *size_of_png_buffer));

    if (png_buffer == NULL){
        memcpy(new_buffer, array_to_concat, size_of_array_to_concat);

    } else {
        // Copy arr1 to the beginning of combinedArr
        memcpy(new_buffer, png_buffer, *size_of_png_buffer);
        
        // Copy arr2 to the end of combinedArr
        memcpy(new_buffer + *size_of_png_buffer, array_to_concat, size_of_array_to_concat);
        free(png_buffer);
    }

    *size_of_png_buffer += size_of_array_to_concat;

    return new_buffer;
}

void write_png(unsigned char *IDAT_concat_Data_def, unsigned int total_height, unsigned int pngWidth, size_t IDAT_Concat_Data_Def_Len) {
    printf("IDAT_concat_Data_def: ");
for (size_t i = 0; i < IDAT_Concat_Data_Def_Len; i++) {
    printf("%02X ", IDAT_concat_Data_def[i]);  // Print byte in hexadecimal format
    if ((i + 1) % 16 == 0)
        printf("\n");  // Print a new line every 16 bytes
}
printf("\n");

printf("Total height: %u\n", total_height);
printf("PNG width: %u\n", pngWidth);
printf("IDAT_Concat_Data_Def_Len: %zu\n", IDAT_Concat_Data_Def_Len);
    struct data_IHDR IHDR_data;
    IHDR_data.height = ntohl(total_height);
    IHDR_data.width = ntohl(pngWidth);
    IHDR_data.bit_depth = 8;
    IHDR_data.color_type = 6;
    IHDR_data.compression = 0;
    IHDR_data.filter = 0;
    IHDR_data.interlace = 0;

    unsigned char* new_png_buffer = malloc(8 + (4 + 4 + 13 + 4) + 4 + 4 + IDAT_Concat_Data_Def_Len + 4 + 12); // header + IHDR + IDAT + IEND

    //Writing header
    unsigned char headerArray[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    FILE *png_all = fopen("all.png", "wb"); // Create file if not already exists. Append to it.
    memcpy(new_png_buffer, &headerArray, 8);

    // Writing IHDR chunk
    unsigned int IHDR_length = ntohl(13);
    unsigned char IHDR_type[] = "IHDR";
    unsigned int U32_IHDR_Data_Buffer[] = {IHDR_data.width, IHDR_data.height};
    unsigned char U8_IHDR_Data_Buffer[] = {IHDR_data.bit_depth, IHDR_data.color_type, IHDR_data.compression, IHDR_data.filter, IHDR_data.interlace};
    memcpy(new_png_buffer + HEADER_SIZE, &IHDR_length, 4);
    memcpy(new_png_buffer + HEADER_SIZE + 4, &IHDR_type, 4);
    memcpy(new_png_buffer + HEADER_SIZE + 4 + 4, &U32_IHDR_Data_Buffer, sizeof(U32) * 2);
    memcpy(new_png_buffer + HEADER_SIZE + 4 + 4 + 8, &U8_IHDR_Data_Buffer, 5);
    unsigned int crc_val = ntohl(crc(new_png_buffer + 8 + 4, 13 + 4));
    memcpy(new_png_buffer + HEADER_SIZE + 4 + 4 + 13, &crc_val, 4);

    // Writing IDAT chunk
    unsigned int IDAT_length = ntohl(IDAT_Concat_Data_Def_Len);
    unsigned char IDAT_type[] = "IDAT";
    memcpy(new_png_buffer + HEADER_SIZE + IHDR_CHUNK_SIZE, &IDAT_length, 4);
    memcpy(new_png_buffer + HEADER_SIZE + IHDR_CHUNK_SIZE + 4, &IDAT_type, 4);
    memcpy(new_png_buffer + HEADER_SIZE + IHDR_CHUNK_SIZE + 8, IDAT_concat_Data_def, IDAT_Concat_Data_Def_Len);
    crc_val = ntohl(crc(new_png_buffer + HEADER_SIZE + IHDR_CHUNK_SIZE + 4, IDAT_Concat_Data_Def_Len + 4));
    memcpy(new_png_buffer + HEADER_SIZE + IHDR_CHUNK_SIZE + 8 + IDAT_Concat_Data_Def_Len, &crc_val, 4);
    
    // Writing IEND chunk
    int IEND_CHUNK_START = HEADER_SIZE + IHDR_CHUNK_SIZE + 8 + IDAT_Concat_Data_Def_Len + 4;
    unsigned char IEND_Chunk[] = {0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    memcpy(new_png_buffer + IEND_CHUNK_START, &IEND_Chunk, 8);
    crc_val = ntohl(crc(new_png_buffer + IEND_CHUNK_START + 4 , 4));
    memcpy(new_png_buffer + IEND_CHUNK_START + 8, &crc_val, 4);

    fwrite(new_png_buffer, HEADER_SIZE + IHDR_CHUNK_SIZE + 8 + IDAT_Concat_Data_Def_Len + 4 + 8 + 4, 1, png_all);

    free(new_png_buffer);
    fclose(png_all);
}

unsigned int get_png_idat_data_length(const uint8_t *memory) {
    // Assuming IDAT_OFFSET_BYTES is the offset in bytes where the length is stored

    unsigned int idat_data_length = 0;

    // Extract the length from memory
    idat_data_length |= ((unsigned int)memory[IDAT_OFFSET_BYTES]) << 24;
    idat_data_length |= ((unsigned int)memory[IDAT_OFFSET_BYTES + 1]) << 16;
    idat_data_length |= ((unsigned int)memory[IDAT_OFFSET_BYTES + 2]) << 8;
    idat_data_length |= ((unsigned int)memory[IDAT_OFFSET_BYTES + 3]);

    // Convert native little endian to big endian and return the number
    return htonl(idat_data_length);
}


void get_png_data_IDAT(unsigned char *out, unsigned char *mem_data, long offset, int idat_data_length) {
    // Calculate the memory location based on the offset.
    unsigned char *mem_location = mem_data + offset;

    // Copy the compressed IDAT data from the memory location.
    for (int i = 0; i < idat_data_length; i++) {
        out[i] = mem_location[i];
    }
}

int get_png_data_IHDR_mem(struct data_IHDR *out, char *memory, size_t offset, size_t size) {
    // Setting default values
    out->height = 0;
    out->width = 0;
    out->bit_depth = 8;
    out->color_type = 6;
    out->compression = 0;
    out->filter = 0;
    out->interlace = 0;

    // Calculate the position in memory to read IHDR Data
    uint32_t width, height;
    
    // Make sure there's enough data to read the width and height
    if (size < offset + 8)
        return 0;  // Error: Not enough data
    
    // Get IHDR width
    width = *((uint32_t*)((char*)memory + offset));
    offset += 4;
    // Get IHDR height
    height = *((uint32_t*)((char*)memory + offset));

    height = htonl(height);
    width = htonl(width);

    out->height = height;
    out->width = width;

    return 1;
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