#include <stdio.h>
#include <stdlib.h>
#include "./starter/png_util/lab_png.h"
#include "./starter/png_util/zutil.h"    /* for mem_def() and mem_inf() */
#include <arpa/inet.h>
#include "./starter/png_util/crc.h"

// Network to host -> Big endian to little endian

// Host to network -> little endian to big endian

// Get image, read image, get png header, determine width and height, get png idat get data from png idat, call mem_inf, copy to buffer.
// Keep track of all heights to update all.png height field.

// To write the new PNG, use fwrite

#define IDAT_OFFSET_BYTES 33 // 33 bytes from start of file to get IDAT chunk start.

#define IHDR_CHUNK_SIZE 25

#define HEADER_SIZE 8

typedef struct data_IHDR *data_IHDR_p;

void deallocate(struct data_IHDR *array[], int size);
int isPng(FILE *file);
int get_png_data_IDATA_length(FILE *fp);
void get_png_data_IDAT(unsigned char *out, FILE *fp, long offset, int whence, int IDAT_length);
unsigned char* concatenatePng(unsigned char *pngBuffer ,unsigned char *arrayToConcat, size_t sizeOfArrayToConcat, size_t *sizeOfPNGBuffer);
void writePng(unsigned char *IDAT_concat_Data_def, unsigned int totalHeight, unsigned int totalWidth, size_t IDAT_concat_Data_Len);

int main(int argc, char* argv[]){
    U64 IDAT_Data_inf_len = 0; /* uncompressed data length */
    size_t IDAT_Concat_Data_Def_Len = 0;

    unsigned int totalHeight = 0;
    unsigned int pngWidth = 0;

    FILE *file;

    unsigned char IDAT_array[argc-1];
    unsigned char *pngBuffer = NULL;
    size_t sizeOfPNGBuffer = 0;
    for (size_t i = 1; i < argc; i++){
        file = fopen(argv[i], "rb");
        data_IHDR_p ihdr = malloc(13);

        if (!isPng(file)){
            continue; // Skip the png file as it is not an image.
        }

        get_png_data_IHDR(ihdr, file, PNG_SIG_SIZE + 4 + 4, SEEK_SET);

        totalHeight += ihdr->height;
        if (pngWidth == 0){
            pngWidth = ihdr->width;
        }
        
        // Now that we have IHDR data, need to retrieve IDAT data. IDAT buffer size = Height * (Width * 4 + 1)
        int IDAT_def_length = ihdr->height * (ihdr->width * 4 + 1);
        unsigned char *IDAT_data_inf = malloc(IDAT_def_length);

        unsigned int IDAT_Data_def_len = get_png_data_IDATA_length(file);
        unsigned char *IDAT_data_def = malloc(IDAT_Data_def_len);

        get_png_data_IDAT(IDAT_data_def, file, IDAT_OFFSET_BYTES, SEEK_SET, IDAT_Data_def_len);

        // decompressing IDAT data.
        mem_inf(IDAT_data_inf, &IDAT_Data_inf_len, IDAT_data_def, IDAT_Data_def_len);

        pngBuffer = concatenatePng(pngBuffer, IDAT_data_inf, IDAT_Data_inf_len, &sizeOfPNGBuffer);

        // free(prev);
        free(ihdr);
        free(IDAT_data_inf);
        free(IDAT_data_def);
        fclose(file);
    }

    // Compress the new concancated IDAT data.
    unsigned char *IDAT_Concat_Data_Def = malloc(totalHeight*(pngWidth * 4 + 1));
    mem_def(IDAT_Concat_Data_Def, &IDAT_Concat_Data_Def_Len, pngBuffer, sizeOfPNGBuffer, Z_DEFAULT_COMPRESSION);
    
    writePng(IDAT_Concat_Data_Def, totalHeight, pngWidth, IDAT_Concat_Data_Def_Len);

    free(IDAT_Concat_Data_Def);
    free(pngBuffer);
    return 0;
}

void writePng(unsigned char *IDAT_concat_Data_def, unsigned int totalHeight, unsigned int pngWidth, size_t IDAT_Concat_Data_Def_Len) {
    struct data_IHDR IHDR_data;
    IHDR_data.height = ntohl(totalHeight);
    IHDR_data.width = ntohl(pngWidth);
    IHDR_data.bit_depth = 8;
    IHDR_data.color_type = 6;
    IHDR_data.compression = 0;
    IHDR_data.filter = 0;
    IHDR_data.interlace = 0;

    unsigned char* newPngBuffer = malloc(8 + (4 + 4 + 13 + 4) + 4 + 4 + IDAT_Concat_Data_Def_Len + 4 + 12); // header + IHDR + IDAT + IEND

    //Writing header
    unsigned char headerArray[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    FILE *pngAll = fopen("all.png", "wb"); // Create file if not already exists. Append to it.
    memcpy(newPngBuffer, &headerArray, 8);

    // Writing IHDR chunk
    unsigned int IHDR_length = ntohl(13);
    unsigned char IHDR_type[] = "IHDR";
    unsigned int U32_IHDR_Data_Buffer[] = {IHDR_data.width, IHDR_data.height};
    unsigned char U8_IHDR_Data_Buffer[] = {IHDR_data.bit_depth, IHDR_data.color_type, IHDR_data.compression, IHDR_data.filter, IHDR_data.interlace};
    memcpy(newPngBuffer + HEADER_SIZE, &IHDR_length, 4);
    memcpy(newPngBuffer + HEADER_SIZE + 4, &IHDR_type, 4);
    memcpy(newPngBuffer + HEADER_SIZE + 4 + 4, &U32_IHDR_Data_Buffer, sizeof(U32) * 2);
    memcpy(newPngBuffer + HEADER_SIZE + 4 + 4 + 8, &U8_IHDR_Data_Buffer, 5);
    unsigned int crc_val = ntohl(crc(newPngBuffer + 8 + 4, 13 + 4));
    memcpy(newPngBuffer + HEADER_SIZE + 4 + 4 + 13, &crc_val, 4);

    // Writing IDAT chunk
    unsigned int IDAT_length = ntohl(IDAT_Concat_Data_Def_Len);
    unsigned char IDAT_type[] = "IDAT";
    memcpy(newPngBuffer + HEADER_SIZE + IHDR_CHUNK_SIZE, &IDAT_length, 4);
    memcpy(newPngBuffer + HEADER_SIZE + IHDR_CHUNK_SIZE + 4, &IDAT_type, 4);
    memcpy(newPngBuffer + HEADER_SIZE + IHDR_CHUNK_SIZE + 8, IDAT_concat_Data_def, IDAT_Concat_Data_Def_Len);
    crc_val = ntohl(crc(newPngBuffer + HEADER_SIZE + IHDR_CHUNK_SIZE + 4, IDAT_Concat_Data_Def_Len + 4));
    memcpy(newPngBuffer + HEADER_SIZE + IHDR_CHUNK_SIZE + 8 + IDAT_Concat_Data_Def_Len, &crc_val, 4);
    
    // Writing IEND chunk
    int IEND_CHUNK_START = HEADER_SIZE + IHDR_CHUNK_SIZE + 8 + IDAT_Concat_Data_Def_Len + 4;
    unsigned char IEND_Chunk[] = {0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    memcpy(newPngBuffer + IEND_CHUNK_START, &IEND_Chunk, 8);
    crc_val = ntohl(crc(newPngBuffer + IEND_CHUNK_START + 4 , 4));
    memcpy(newPngBuffer + IEND_CHUNK_START + 8, &crc_val, 4);

    fwrite(newPngBuffer, HEADER_SIZE + IHDR_CHUNK_SIZE + 8 + IDAT_Concat_Data_Def_Len + 4 + 8 + 4, 1, pngAll);

    free(newPngBuffer);
    fclose(pngAll);
}

void deallocate(struct data_IHDR *array[], int size){
    for (size_t i = 0; i < size; ++i){
        free(array[i]);
    }
}

// Returns 1 or 0. 1 -> Is a png, 0 -> Not a png
int isPng(FILE *file) {
    unsigned char *header = malloc(sizeof(unsigned char) * 8);

    fseek(file, 0, SEEK_SET);
    fread(header, 1, PNG_SIG_SIZE, file);

    unsigned int cmpHeader[] = {137, 80, 78, 71, 13, 10, 26, 10};
    for (int i = 0; i < PNG_SIG_SIZE; ++i){
        unsigned int result = header[i];
        if (result != cmpHeader[i]){
            return 0;
        }
    }

    free(header);
    return 1;
}

int get_png_data_IHDR(struct data_IHDR *out, FILE *fp, long offset, int whence){
    // Setting default values
    out->height = 0;
    out->width = 0;
    out->bit_depth = 8;
    out->color_type = 6;
    out->compression = 0;
    out->filter = 0;
    out->interlace = 0;

    // Seek 8 (header) + 4 (data length) + 4 (type) = 16 bytes to get to IHDR Data
    fseek(fp, offset, whence);

    unsigned int height;
    unsigned int width;

    // Get IHDR width
    fread(&width, 4, 1, fp);
    fread(&height, 4, 1, fp);

    height = htonl(height);
    width = htonl(width);

    out->height = height;
    out->width = width;

    return 1;
}

// Extracts compressed IDAT data size 
int get_png_data_IDATA_length(FILE *fp){
    // Read "Length" portion of IDAT chunk to get IDAT data length
    fseek(fp, IDAT_OFFSET_BYTES, SEEK_SET);

    unsigned int IDAT_Data_length = 0;
    fread(&IDAT_Data_length, 4, 1, fp);

    // Flip native little endian to big endian and return the number
    return htonl(IDAT_Data_length);
}

// Extracts IDAT data
void get_png_data_IDAT(unsigned char *out, FILE *fp, long offset, int whence, int IDAT_Data_length) {
    // Now that we have the IDAT_Data_length, we can now determine the amount of bytes to read to grab the compressed idat data.
    fseek(fp, IDAT_OFFSET_BYTES + 8, SEEK_SET);
    fread(out, 1, IDAT_Data_length, fp);
}

unsigned char* concatenatePng(unsigned char *pngBuffer ,unsigned char *arrayToConcat, size_t sizeOfArrayToConcat, size_t *sizeOfPNGBuffer){
    unsigned char *newBuffer = malloc( sizeof(unsigned char) *  (sizeOfArrayToConcat + *sizeOfPNGBuffer));

    if (pngBuffer == NULL){
        memcpy(newBuffer, arrayToConcat, sizeOfArrayToConcat);

    } else {
        // Copy arr1 to the beginning of combinedArr
        memcpy(newBuffer, pngBuffer, *sizeOfPNGBuffer);
        
        // Copy arr2 to the end of combinedArr
        memcpy(newBuffer + *sizeOfPNGBuffer, arrayToConcat, sizeOfArrayToConcat);
        free(pngBuffer);
    }

    *sizeOfPNGBuffer += sizeOfArrayToConcat;

    return newBuffer;
}