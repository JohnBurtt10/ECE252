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

extern int is_file_png(char* filename);

void listDirectoryContents(const char *directory);

int main(int argc, char *argv[]) 
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <directory name>\n", argv[0]);
        exit(1);
    }

    listDirectoryContents(argv[1]);

    return 0;
}

void listDirectoryContents(const char *directory) 
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
        // Check if directory ends with a '/'
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
                if (strcmp(str_path, ".") == 0 || strcmp(str_path, "..") == 0) {
                    continue;
                }
                
                    // printf("%s %s\n", "entering: ", formatted_path);
                    listDirectoryContents(formatted_path);
            } else if (is_file_png(formatted_path)) {
                printf("%s\n", formatted_path);
            }
            
        }             
    }

    if (closedir(p_dir) != 0) {
        perror("closedir");
        exit(3);
    }
}


// void processDirectory(char *str_path[]) { 
//     struct dirent *p_dirent;
//     struct stat buf;
//     DIR *p_dir;
//     DIR *child_p_dir;
//     char str[64];
//     if ((p_dir = opendir(str_path) == NULL)) {
//         sprintf(str, "opendir(%s)", str_path);
//         perror(str);
//         exit(2);
//     }
//     while ((p_dirent = readdir(p_dir)) != NULL) {
//         char *str_path = p_dirent->d_name;  /* relative path name! */
//         printf("%s\n", str_path);

//         if (str_path == NULL) {
//             fprintf(stderr,"Null pointer found!"); 
//             exit(3);
//         } else {
//             if (lstat(str_path, &buf) < 0) {
//             perror("lstat error");
//             continue;
//             }
//             // if      (S_ISREG(buf.st_mode))  ptr = "regular";
//             else if (S_ISDIR(buf.st_mode))  {
//                 // Skip '.' and '..' directories
//             if (strcmp(str_path, ".") == 0 || strcmp(str_path, "..") == 0) {
//                 continue;
//             }
//                 printf("%s %s\n", "entering: ", str_path);
//             // processDirectory(child_p_dir);
           
//             // // printf("%s, %s\n", "entering child directory: ", buf);
            
//             }             
//             // printf("%s\n", str_path);
//         }
//     }
//     if (closedir(p_dir) != 0 ) {
//             perror("closedir");
//             exit(3);
//     }
// }


// /**
//  * @file:  ECE252/lab1/starter/ls/ls_ftype.c
//  * @brief: Print the type of file for each command-line argument
//  * @author: W. Richard Stevens, Advanced Programming in the UNIX Environment
//  * @date: 2018/10/15
//  */

// #include <sys/types.h>
// #include <sys/stat.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <unistd.h>

// int main(int argc, char *argv[])
// {
//     int i;
//     char *ptr;
//     struct stat buf;

//     for (i = 1; i < argc; i++) {
//         printf("%s: ", argv[i]);
//         if (lstat(argv[i], &buf) < 0) {
//             perror("lstat error");
//             continue;
//         }   

//         if      (S_ISREG(buf.st_mode))  ptr = "regular";
//         else if (S_ISDIR(buf.st_mode))  ptr = "directory";
//         else if (S_ISCHR(buf.st_mode))  ptr = "character special";
//         else if (S_ISBLK(buf.st_mode))  ptr = "block special";
//         else if (S_ISFIFO(buf.st_mode)) ptr = "fifo";
// #ifdef S_ISLNK
//         else if (S_ISLNK(buf.st_mode))  ptr = "symbolic link";
// #endif
// #ifdef S_ISSOCK
//         else if (S_ISSOCK(buf.st_mode)) ptr = "socket";
// #endif
//         else                            ptr = "**unknown mode**";
//         printf("%s\n", ptr);
//     }
//     return 0;
// }
