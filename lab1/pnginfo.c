#include <stdio.h>
#include <stdlib.h>
#include "./starter/png_util/lab_png.h"
#include "pnginfo.h"

typedef unsigned char U8;
typedef unsigned int  U32;

typedef struct chunk *chunk_p;
typedef struct data_IHDR *data_IHDR_p;

typedef struct simple_PNG *simple_PNG_p;

/**
 * @brief Processes a PNG file and prints its dimensions.
 *
 * This function opens the specified PNG file, reads its header and IHDR chunk
 * to determine the width and height of the image, and prints the dimensions
 * to the console. The file is then closed, and memory allocated for IHDR data
 * is freed.
 *
 * @param filename The path to the PNG file to be processed.
 * @return 0 upon successful processing of the PNG file.
 */
int process_png(const char* filename) { 
    FILE *file = fopen(filename, "rb");
    
    unsigned char header[8];

    fread(header, 1, PNG_SIG_SIZE, file);

    // Now read IHDR chunk
    struct data_IHDR *ihdr = malloc(13);
    get_png_data_IHDR(ihdr, file, PNG_SIG_SIZE + 4 + 4, SEEK_SET);

    printf("%s: %d x %d\n", filename, ihdr->height, ihdr->width);

    fclose(file);

    free(ihdr);

    return 0;
}

/**
 * @brief Checks if a file is a PNG image based on its header.
 *
 * This function attempts to open the specified file and reads its header
 * to determine if it conforms to the PNG image format. If the file cannot
 * be opened, it returns 0. If the file is successfully opened and the header
 * indicates it's a PNG image, it returns 1. Otherwise, it returns 0.
 *
 * @param filename The path to the file to be checked.
 * @return 1 if the file is a PNG image, 0 otherwise or on error.
 */
int is_file_png(char* filename) { 
    
    FILE *file = fopen(filename, "rb");

    if (file == NULL) {
        // perror("Error opening file");
        return 0;
    }
    
    unsigned char header[8];

    fread(header, 1, PNG_SIG_SIZE, file);

    int isitPng = is_png(header, PNG_SIG_SIZE);

    fclose(file);

    return isitPng;

}

/**
 * @brief Checks if the provided buffer contains a valid PNG header.
 *
 * This function compares the provided buffer with a standard PNG header
 * to determine if it represents a valid PNG image. The standard PNG header
 * consists of the first 8 bytes, which are compared against predefined values.
 * If the buffer matches the PNG header, indicating it's a PNG image, the function
 * returns 1. Otherwise, it returns 0.
 *
 * @param buf Pointer to the buffer containing the data to be checked.
 * @param n The number of bytes to check in the buffer (should be PNG_SIG_SIZE or more).
 * @return 1 if the buffer represents a valid PNG image, 0 otherwise.
 */
int is_png(U8 *buf, size_t n) {
    unsigned int cmpHeader[] = {137, 80, 78, 71, 13, 10, 26, 10};
    for (int i = 0; i < PNG_SIG_SIZE; ++i){
        unsigned int result = buf[i];
        if (result != cmpHeader[i]){
            return 0;
        }
    }

    return 1;
}

/**
 * @brief Retrieves PNG IHDR data from a file and populates the provided struct.
 *
 * This function reads the PNG IHDR data (width and height) from the specified
 * file at the given offset, and populates the provided struct with the extracted
 * width and height values. Default values for other IHDR properties are set in
 * the provided struct. The file is expected to be at the correct position for
 * reading IHDR data before calling this function.
 *
 * @param out Pointer to a struct data_IHDR where extracted data will be stored.
 * @param fp File pointer to the opened PNG file.
 * @param offset Offset from the starting position (based on 'whence') to read IHDR data.
 * @param whence Specifies the starting position for seeking within the file (SEEK_SET, SEEK_CUR, SEEK_END).
 * @return 1 upon successful extraction of IHDR data and population of the struct.
 */
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
    fread(&height, 4, 1, fp);
    fread(&width, 4, 1, fp);

    height = ((height>>24)&0xff) | // move byte 3 to byte 0
             ((height<<8)&0xff0000) | // move byte 1 to byte 2
             ((height>>8)&0xff00) | // move byte 2 to byte 1
             ((height<<24)&0xff000000); // byte 0 to byte 3

    width =  ((width>>24)&0xff) |
             ((width<<8)&0xff0000) |
             ((width>>8)&0xff00) |
             ((width<<24)&0xff000000);

    out->height = height;
    out->width = width;

    return 1;
}