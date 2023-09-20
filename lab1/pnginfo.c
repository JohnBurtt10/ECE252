#include <stdio.h>
#include <stdlib.h>
#include "./starter/png_util/lab_png.h"
#include "pnginfo.h"

typedef unsigned char U8;
typedef unsigned int  U32;

typedef struct chunk *chunk_p;
typedef struct data_IHDR *data_IHDR_p;

typedef struct simple_PNG *simple_PNG_p;

// does not check if it's a valid png
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

// Returns 1 or 0. 1 -> Is a png, 0 -> Not a png

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

// Returns 1 or 0. 1 -> Is a png, 0 -> Not a png
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