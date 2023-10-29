#include <semaphore.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct _args {
    int bufferSize;
    int numProducers;
    int numConsumers;
    int numMilliseconds;
    int imageToFetch;
} args;

args arguments;

void processInput(int argc, char *argv[], args* destination);

int main(int argc, char* argv[]){
    processInput(argc, argv, &arguments);

    return 0;
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