#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

typedef struct _args {
    int numThreads;
    int numUniquePNGs;
    char* logFile;
    char* startingURL;
} args;

args arguments;

int processInput(args* arguments, char* argv[], int argc);

int main(int argc, char* argv[]){
    processInput(&arguments, argv, argc);

    printf("numThreads: %d, numUniquePngs: %d, logFile: %s, startingURL: %s\n", arguments.numThreads, arguments.numUniquePNGs, arguments.logFile, arguments.startingURL);

    return 0;
}

int processInput(args* arguments, char* argv[], int argc){
    int c;
    arguments->numUniquePNGs = 50;
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
            arguments->numUniquePNGs = atoi(optarg);
            if (arguments->numUniquePNGs < 0) {
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
}