#include <stdio.h>
#include <stdlib.h>
#include "./starter/png_util/lab_png.h"
#include "./starter/png_util/zutil.h"    /* for mem_def() and mem_inf() */
#include <arpa/inet.h>
#include "./starter/png_util/crc.h"
#include "catpng.h"

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
int get_png_idat_data_length(FILE *fp);
void get_png_data_IDAT(unsigned char *out, FILE *fp, long offset, int whence, int IDAT_length);
unsigned char* concatenate_png(unsigned char *png_buffer ,unsigned char *array_to_concat, size_t size_of_array_to_concat, size_t *size_of_png_buffer);
void write_png(unsigned char *IDAT_concat_Data_def, unsigned int total_height, unsigned int totalWidth, size_t IDAT_concat_Data_Len);

int main(int argc, char* argv[]){
    cat_png(argc, argv);
}

/**
 * @brief Concatenates PNG image data from multiple files and creates a new PNG file.
 *
 * This function concatenates the IDAT data from multiple PNG files specified in the
 * command line arguments. It calculates the total height and width of the new PNG image
 * based on the individual PNG files. The concatenated IDAT data is compressed, and a new
 * PNG file named "all.png" is created with the concatenated and compressed data.
 *
 * @param argc Number of command line arguments.
 * @param argv Array of command line argument strings containing PNG file paths.
 */
void cat_png(int argc, char* argv[]) { 
    U64 IDAT_Data_inf_len = 0; /* uncompressed data length */
    size_t IDAT_Concat_Data_Def_Len = 0;

    unsigned int total_height = 0;
    unsigned int pngWidth = 0;

    FILE *file;

    unsigned char IDAT_array[argc-1];
    unsigned char *png_buffer = NULL;
    size_t size_of_png_buffer = 0;
    for (size_t i = 1; i < argc; i++){
        file = fopen(argv[i], "rb");
        data_IHDR_p ihdr = malloc(13);

        if (!isPng(file)){
            free(ihdr);
            free(png_buffer);
            fclose(file);
            return; // Skip the png file as it is not an image.
        }

        get_png_data_IHDR(ihdr, file, PNG_SIG_SIZE + 4 + 4, SEEK_SET);

        total_height += ihdr->height;
        if (pngWidth == 0){
            pngWidth = ihdr->width;
        }
        
        // Now that we have IHDR data, need to retrieve IDAT data. IDAT buffer size = Height * (Width * 4 + 1)
        int IDAT_def_length = ihdr->height * (ihdr->width * 4 + 1);
        unsigned char *IDAT_data_inf = malloc(IDAT_def_length);

        unsigned int IDAT_Data_def_len = get_png_idat_data_length(file);
        unsigned char *IDAT_data_def = malloc(IDAT_Data_def_len);

        get_png_data_IDAT(IDAT_data_def, file, IDAT_OFFSET_BYTES, SEEK_SET, IDAT_Data_def_len);

        // decompressing IDAT data.
        mem_inf(IDAT_data_inf, &IDAT_Data_inf_len, IDAT_data_def, IDAT_Data_def_len);

        png_buffer = concatenate_png(png_buffer, IDAT_data_inf, IDAT_Data_inf_len, &size_of_png_buffer);

        // free(prev);
        free(ihdr);
        free(IDAT_data_inf);
        free(IDAT_data_def);
        fclose(file);
    }

    // Compress the new concancated IDAT data.
    unsigned char *IDAT_Concat_Data_Def = malloc(total_height*(pngWidth * 4 + 1));
    mem_def(IDAT_Concat_Data_Def, &IDAT_Concat_Data_Def_Len, png_buffer, size_of_png_buffer, Z_DEFAULT_COMPRESSION);
    
    write_png(IDAT_Concat_Data_Def, total_height, pngWidth, IDAT_Concat_Data_Def_Len);

    free(IDAT_Concat_Data_Def);
    free(png_buffer);
}

/**
 * @brief Writes PNG data to a file based on the provided IDAT data and IHDR parameters.
 *
 * This function constructs a PNG file by combining the header, IHDR chunk, IDAT chunk,
 * and IEND chunk using the provided IDAT data and IHDR parameters. The resulting PNG
 * data is written to a file named "all.png".
 *
 * @param IDAT_concat_Data_def Pointer to the concatenated IDAT data.
 * @param total_height The total height of the PNG image.
 * @param pngWidth The width of the PNG image.
 * @param IDAT_Concat_Data_Def_Len The length of the concatenated IDAT data.
 */
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

void deallocate(struct data_IHDR *array[], int size){
    for (size_t i = 0; i < size; ++i){
        free(array[i]);
    }
}

/**
 * @brief Checks if the given file is a PNG image.
 *
 * This function reads the header of the provided file and compares it against
 * a standard PNG header to determine if the file is a valid PNG image. If the
 * file header matches the PNG header, indicating it's a PNG image, the function
 * returns 1. Otherwise, it returns 0.
 *
 * @param file Pointer to the file to be checked.
 * @return 1 if the file is a PNG image, 0 otherwise.
 */
int isPng(FILE *file) {
    unsigned char *header = malloc(sizeof(unsigned char) * 8);

    fseek(file, 0, SEEK_SET);
    int numBytes = fread(header, 1, PNG_SIG_SIZE, file);

    if (numBytes < PNG_SIG_SIZE){
        free(header);
        return 0;
    }

    unsigned int cmp_header[] = {137, 80, 78, 71, 13, 10, 26, 10};
    for (int i = 0; i < PNG_SIG_SIZE; ++i){
        unsigned int result = header[i];
        if (result != cmp_header[i]){
            free(header);
            return 0;
        }
    }

    free(header);
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
    fread(&width, 4, 1, fp);
    fread(&height, 4, 1, fp);

    height = htonl(height);
    width = htonl(width);

    out->height = height;
    out->width = width;

    return 1;
}

/**
 * @brief Retrieves the length of PNG IDAT data from the file's IDAT chunk.
 *
 * This function reads the "Length" portion of the IDAT chunk from the specified
 * PNG file and returns the IDAT data length. It assumes that the file is at the
 * correct position to read the IDAT chunk length before calling this function.
 *
 * @param fp File pointer to the opened PNG file.
 * @return The length of the IDAT data in network byte order (big-endian).
 */
int get_png_idat_data_length(FILE *fp){
    // Read "Length" portion of IDAT chunk to get IDAT data length
    fseek(fp, IDAT_OFFSET_BYTES, SEEK_SET);

    unsigned int idat_data_length = 0;
    fread(&idat_data_length, 4, 1, fp);

    // Flip native little endian to big endian and return the number
    return htonl(idat_data_length);
}

/**
 * @brief Retrieves PNG IDAT data from a file and stores it in the provided buffer.
 *
 * This function reads the PNG IDAT data from the specified file at the given offset,
 * and stores the compressed IDAT data in the provided output buffer. The amount of
 * data to read is determined by the given IDAT data length. The file is expected to
 * be at the correct position for reading IDAT data before calling this function.
 *
 * @param out Pointer to the buffer where the IDAT data will be stored.
 * @param fp File pointer to the opened PNG file.
 * @param offset Offset from the starting position (based on 'whence') to read IDAT data.
 * @param whence Specifies the starting position for seeking within the file (SEEK_SET, SEEK_CUR, SEEK_END).
 * @param idat_data_length Length of the IDAT data to be read and stored.
 */
void get_png_data_IDAT(unsigned char *out, FILE *fp, long offset, int whence, int idat_data_length) {
    // Now that we have the idat_data_length, we can now determine the amount of bytes to read to grab the compressed idat data.
    fseek(fp, IDAT_OFFSET_BYTES + 8, SEEK_SET);
    fread(out, 1, idat_data_length, fp);
}

/**
 * @brief Concatenates an array to a PNG buffer.
 *
 * This function concatenates the provided array to a PNG buffer. If the PNG buffer is NULL,
 * a new buffer is created and populated with the contents of the array. If the PNG buffer
 * is not NULL, the array is appended to the existing PNG buffer. The size of the resulting
 * PNG buffer is updated accordingly.
 *
 * @param png_buffer Pointer to the existing PNG buffer (can be NULL).
 * @param array_to_concat Pointer to the array to be concatenated.
 * @param size_of_array_to_concat Size of the array to be concatenated.
 * @param size_of_png_buffer Pointer to the size of the PNG buffer (updated after concatenation).
 * @return Pointer to the newly concatenated PNG buffer.
 */
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