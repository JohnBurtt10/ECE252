/**
 * @file: ls_fname.c
 * @brief: simple ls command to list file names of a directory 
 */

#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>   /* for printf().  man 3 printf */
#include <stdlib.h>  /* for exit().    man 3 exit   */
#include <string.h>  /* for strcat().  man strcat   */
#include "./starter/png_util/lab_png.h"
#include "pnginfo.h"

int png_found = 0;

extern int is_file_png(char* filename);

void list_directory_contents(const char *directory);

int main(int argc, char *argv[]) 
{
    if (argc != 2) {
        fprintf(stderr, "Usawge: %s <directory name>\n", argv[0]);
        exit(1);
    }

    list_directory_contents(argv[1]);
    char no_png_found_message[] = "findpng: No PNG file found\n";
    if (png_found == 0) { 
        printf("%s", no_png_found_message);
    }

    return 0;
}


/**
 * @brief Lists the contents of a directory, recursively checking for PNG files.
 *
 * This function traverses the specified directory, listing its contents and
 * recursively exploring subdirectories. For each entry encountered, it checks if
 * the entry is a PNG file using the `is_file_png` function. If the entry is a PNG
 * file, it prints the path to the console. The traversal is performed recursively
 * for subdirectories. The directory path should be provided to initiate the traversal.
 *
 * @param directory The path to the directory whose contents will be listed.
 */
void list_directory_contents(const char *directory) 
{
    DIR *p_dir;
    struct dirent *p_dirent;
    struct stat buf;

    if ((p_dir = opendir(directory)) == NULL) {
        printf("error with: %s", directory);
        perror("opendir");
        exit(2);
    }

    while ((p_dirent = readdir(p_dir)) != NULL) {
        char *str_path = p_dirent->d_name;  /* relative path name! */
        char formatted_path[64];
        // Check if directory already ends with a '/'
        if (directory[strlen(directory) - 1] == '/') {
            sprintf(formatted_path, "%s%s", directory, str_path);
        } else {
            sprintf(formatted_path, "%s/%s", directory, str_path);
        }
        if (str_path == NULL) {
            fprintf(stderr,"Null pointer found!"); 
            exit(3);
        } else {
            if (p_dirent->d_type == DT_DIR) {
                // skip '.' and '..' directories
                if (strcmp(str_path, ".") == 0 || strcmp(str_path, "..") == 0) {
                    continue;
                }
                    // recursively call children nodes
                    list_directory_contents(formatted_path);
            } else if (is_file_png(formatted_path)) {
                png_found = 1;
                printf("%s\n", formatted_path);
            }
            
        }             
    }

    if (closedir(p_dir) != 0) {
        perror("closedir");
        exit(3);
    }
}