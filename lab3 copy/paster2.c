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
#include <arpa/inet.h>

#include "../lab1/starter/png_util/zutil.h"    /* for mem_def() and mem_inf() */
#include "../lab1/starter/png_util/lab_png.h"
#include "../lab1/starter/png_util/crc.h"

#define BUF_SIZE 10000  /* 1024*1024 = 1M */

#define BUF_INC  500   /* 1024*512  = 0.5M */

#define IDAT_OFFSET_BYTES 33 // 33 bytes from start of file to get IDAT chunk start.


#define IHDR_SIZE 25
#define HEADER_SIZE 8
#define IDAT_SIZE_UNCOMPRESSED 12 + (400*(6*4 + 1))
#define IEND_SIZE 12

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
int imageSegmentSize_decompressed = HEADER_SIZE + IHDR_SIZE + (IDAT_SIZE_UNCOMPRESSED) + IEND_SIZE;
int imageSegmentIDAT_uncompressed_size = 400*(6*4) + 6; // Why da fuck

void signal_handler(int sig);

void processInput(int argc, char *argv[], args* destination);
int producerCheckAndSet();
void createRequest(unsigned int imageSequence);
void pushToStack(RECV_BUF* imageData);
void popFromStack(RECV_BUF *returnedData);
unsigned int index_decompressed_idats(int imageSeq);
void decompress_and_push(RECV_BUF *image);
unsigned int get_png_idat_data_length(const unsigned char* png);
void get_png_data_IDAT(unsigned char *out, unsigned char *mem_data, long offset, int idat_data_length);
void create_image();
void write_png(unsigned char *IDAT_concat_Data_def, unsigned int total_height, unsigned int pngWidth, size_t IDAT_Concat_Data_Def_Len);

/* Cleaning functions */
void destory_sems();
void detach_and_clean();

void recv_buf_init(RECV_BUF *ptr);
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata); 
size_t write_cb_curl(char *p_recv, size_t size, size_t nmemb, void *p_userdata);

/* Attached memory variables */
IMGSTACK* pstack;
int* imageToFetch;
char* uncompressedIDATs;
unsigned long* idatLength;
/* Shared memory ID's */
int shmid_stack;
int shmid_imageToFetch;
int shmid_uncompressedIDATs;
int shmid_idatLength;

int main(int argc, char* argv[]){
    processInput(argc, argv, &arguments);

    unsigned int itt;

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

    // Creating shared memory to store image segments that have been decompressed by consumers
    shmid_uncompressedIDATs = shmget(IPC_PRIVATE, imageSegmentIDAT_uncompressed_size * 50, IPC_CREAT | 0666);
    uncompressedIDATs = shmat(shmid_uncompressedIDATs, NULL, 0);
    memset(uncompressedIDATs, 0, imageSegmentIDAT_uncompressed_size * 50); // Setting shared memory data to all zero.

    // Creating shared memory to track true idat size
    shmid_idatLength = shmget(IPC_PRIVATE, sizeof(unsigned long), IPC_CREAT | 0666);
    idatLength = shmat(shmid_idatLength, NULL, 0);
    *idatLength = 0;

    // Create producers
    for(int i = 0; i < arguments.numProducers; ++i){
        int pid = fork();
        
        // child
        if (pid == 0){
            while(1){
                // return current stored image count, then increment by one
                sem_wait(&pstack->imageToFetch_sem);
                itt = producerCheckAndSet();
                sem_post(&pstack->imageToFetch_sem);
                
                // Check if we have fetched all 50 images.
                if (itt == -1){
                    exit(0);
                }

                // Crete fetch request and push to stack.
                createRequest(itt);
            }
        }
    }

    // Create the consumers
    for (int i = 0; i < arguments.numConsumers; ++i){
        int pid = fork();

        if (pid == 0){
            while(1){
                RECV_BUF* image = malloc(sizeof(RECV_BUF));

                // If the decrement cannot be performed, it will return EAGAIN, thus we exit.
                if (sem_trywait(&pstack->consumedCount_sem)) {
                    exit(0);
                }

                // Pop image from stack/shared buffer
                popFromStack(image);
                printf("IMAGE SEQ: %d\n", image->seq);

                // decompress image and store it in final shared memory location
                sem_wait(&pstack->pushImage_sem);
                decompress_and_push(image);
                sem_post(&pstack->pushImage_sem);

                free(image);
            }
        }
    }

    // Wait for all processes to finish
    while(wait(NULL) > 0){};

    // Create image.
    create_image();

    printf("FINISHED!\n");

    // Cleaning up
    detach_and_clean();
    destory_sems();

    return 0;
}

void destory_sems(){
    sem_destroy(&pstack->buffer_sem);
    sem_destroy(&pstack->spaces_sem);
    sem_destroy(&pstack->imageToFetch_sem);
    sem_destroy(&pstack->items_sem);
    sem_destroy(&pstack->pushImage_sem);
    sem_destroy(&pstack->consumedCount_sem);
    sem_destroy(&pstack->incrementIDATLen_sem);
}

void detach_and_clean(){
    shmdt(pstack);
    shmctl(shmid_stack, IPC_RMID, NULL);

    shmdt(imageToFetch);
    shmctl(shmid_imageToFetch, IPC_RMID, NULL);

    shmdt(uncompressedIDATs);
    shmctl(shmid_uncompressedIDATs, IPC_RMID, NULL);

    shmdt(idatLength);
    shmctl(shmid_idatLength, IPC_RMID, NULL);
}

void decompress_and_push(RECV_BUF *image){
    U64 IDAT_Data_inf_len = 0; /* uncompressed data length */
    size_t IDAT_Concat_Data_Def_Len = 0;
    unsigned char *png_buffer = NULL;
    size_t size_of_png_buffer = 0;
        
    // Now that we have IHDR data, need to retrieve IDAT data. IDAT buffer size = Height * (Width * 4 + 1)
    int IDAT_inf_length = imageSegmentIDAT_uncompressed_size;
    unsigned char *IDAT_data_inf = malloc(IDAT_inf_length);
    unsigned int IDAT_Data_def_len = get_png_idat_data_length(image->buf);
    unsigned char *IDAT_data_def = malloc(IDAT_Data_def_len);

    get_png_data_IDAT(IDAT_data_def, image->buf, IDAT_OFFSET_BYTES+8, IDAT_Data_def_len);

    // decompressing IDAT data
    mem_inf(IDAT_data_inf, &IDAT_Data_inf_len, IDAT_data_def, IDAT_Data_def_len);
    *idatLength += IDAT_Data_inf_len;
    printf("IDATLENGTHINFLATED: %lu", IDAT_Data_inf_len);

    // Add uncompressed idat data to shared memory.
    memcpy(uncompressedIDATs + index_decompressed_idats(image->seq), IDAT_data_inf, IDAT_Data_inf_len);

    free(IDAT_data_inf);
    free(IDAT_data_def);
}

void create_image(){
    // First, compress the IDATS.
    size_t IDAT_Concat_Data_Def_Len = 0;
    unsigned char *IDAT_Concat_Data_Def = malloc(400*(300 * 4 + 1));
    printf("IDATlength: %lu\n", *idatLength);
    mem_def(IDAT_Concat_Data_Def, &IDAT_Concat_Data_Def_Len, uncompressedIDATs, *idatLength, Z_DEFAULT_COMPRESSION);
    printf("IDATDEFLENGTH: %lu\n", IDAT_Concat_Data_Def_Len);

    // Second, write the png after compressing idats.
    write_png(IDAT_Concat_Data_Def, 300, 400, IDAT_Concat_Data_Def_Len);

    free(IDAT_Concat_Data_Def);
}

unsigned int get_png_idat_data_length(const unsigned char* png) {
    // Assuming IDAT_OFFSET_BYTES is the offset in bytes where the length is stored

    unsigned int idat_data_length = 0;

    // Extract the length from memory
    memcpy(&idat_data_length, png + IDAT_OFFSET_BYTES, 4);

    // Convert native little endian to big endian and return the number
    return htonl(idat_data_length);
}

void get_png_data_IDAT(unsigned char *out, unsigned char *mem_data, long offset, int idat_data_length) {
    // Copy the compressed IDAT data from the memory location.
    for (int i = 0; i < idat_data_length; i++) {
        out[i] = mem_data[i + offset];
    }
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

void saveImages(){
    // popping stack and printing contents
    for(int i = 0; i < arguments.numProducers; i++){
        RECV_BUF image;
        char test[50];
        pop(pstack, &image);
        printf("Popped Image seq: %d\n", image.seq);

        sprintf(test, "%d.png", image.seq);
        FILE *fp = fopen(test, "wb");
        fwrite(image.buf, 1, 10000, fp);
        fclose(fp);
    }
}

void recv_buf_init(RECV_BUF *ptr)
{
    memset(ptr->buf, 0, 10000);
    ptr->size = 0;
    ptr->seq = -1;              /* valid seq should be non-negative */
}

int producerCheckAndSet() {
    unsigned int return_val;
    if (*imageToFetch >= 50) { 
        return -1;
    }
    return_val = *imageToFetch;
    *imageToFetch = *imageToFetch + 1;
    return return_val;
}

void createRequest(unsigned int image_segment){
    RECV_BUF* recv_buf = malloc(sizeof(RECV_BUF));

    CURL* curl_handle;
    curl_handle = curl_easy_init();

    if (curl_handle == NULL){
        printf("Failed to initalize CURL");
        return;
    }    

    char url[60];

    sprintf(url, "%s%d", urls[1][arguments.imageToFetch-1], image_segment);

    printf("processID # %d fetching image strip: %d\n", getpid(), image_segment);

    /* specify URL to get */
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);

    /* register write call back function to process received data */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)recv_buf);

    /* register header call back function to process received header data */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)recv_buf);

    /* some servers requires a user-agent field */
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    recv_buf_init(recv_buf);
    CURLcode res = curl_easy_perform(curl_handle);

    if (res == CURLE_OK){
        pushToStack(recv_buf);
    } else {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    }

    // Free the resource. No longer needed as we stored the contents in the sharedmemory.
    free(recv_buf);

    curl_easy_cleanup(curl_handle);

    return;
}

// Push the image to the stack only when the stack/buffer is not full and the producer has access to it.
void pushToStack(RECV_BUF* imageData){
    sem_wait(&pstack->spaces_sem);
    sem_wait(&pstack->buffer_sem);
    printf("Start pushing img seq: %d\n", imageData->seq);
    push(pstack, imageData);
    sem_post(&pstack->items_sem);
    sem_post(&pstack->buffer_sem);    
    printf("Done pushing\n");
}

// Remove compressed image from shared stack/buffer
void popFromStack(RECV_BUF *returnedData){
    sem_wait(&pstack->items_sem);
    // Binary semaphore. Do not pop unless no consumer/producer are currently accessing stack/buffer.
    sem_wait(&pstack->buffer_sem);
    pop(pstack, returnedData);
    printf("Popped from stack image seq: %d\n", returnedData->seq);
    sem_post(&pstack->buffer_sem);
    sem_post(&pstack->spaces_sem);
    printf("Done popping\n");
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
    
    // printf("Realsize: %d, strlen: %lu, strncmp: %d\n", realsize, strlen(ECE252_HEADER), strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)));
    if (realsize > strlen(ECE252_HEADER) && strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0) {
        /* extract img sequence number */
        printf("Seq_num: %d\n", atoi(p_recv + strlen(ECE252_HEADER)));
	    p->seq = atoi(p_recv + strlen(ECE252_HEADER));
    }

    return realsize;
}

// Index the shared memory region by incrementing the pointer
unsigned int index_decompressed_idats(int imageSeq){
    // Each decompressed image is of size "imageSegmentSize", so we must index by that.

    // Eg, seq = 1, should point us to an address imageSegmentSize away from index 0
    return imageSeq * imageSegmentIDAT_uncompressed_size;
}

void write_png(unsigned char *IDAT_concat_Data_def, unsigned int total_height, unsigned int pngWidth, size_t IDAT_Concat_Data_Def_Len) {
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
    memcpy(new_png_buffer + HEADER_SIZE + IHDR_SIZE, &IDAT_length, 4);
    memcpy(new_png_buffer + HEADER_SIZE + IHDR_SIZE + 4, &IDAT_type, 4);
    memcpy(new_png_buffer + HEADER_SIZE + IHDR_SIZE + 8, IDAT_concat_Data_def, IDAT_Concat_Data_Def_Len);
    crc_val = ntohl(crc(new_png_buffer + HEADER_SIZE + IHDR_SIZE + 4, IDAT_Concat_Data_Def_Len + 4));
    memcpy(new_png_buffer + HEADER_SIZE + IHDR_SIZE + 8 + IDAT_Concat_Data_Def_Len, &crc_val, 4);
    
    // Writing IEND chunk
    int IEND_CHUNK_START = HEADER_SIZE + IHDR_SIZE + 8 + IDAT_Concat_Data_Def_Len + 4;
    unsigned char IEND_Chunk[] = {0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    memcpy(new_png_buffer + IEND_CHUNK_START, &IEND_Chunk, 8);
    crc_val = ntohl(crc(new_png_buffer + IEND_CHUNK_START + 4 , 4));
    memcpy(new_png_buffer + IEND_CHUNK_START + 8, &crc_val, 4);

    fwrite(new_png_buffer, HEADER_SIZE + IHDR_SIZE + 8 + IDAT_Concat_Data_Def_Len + 4 + 8 + 4, 1, png_all);

    free(new_png_buffer);
    fclose(png_all);
}