#include <semaphore.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct _args {
    int bufferSize; // Buffer to hold image segments. Less than 50.
    int numProducers; // Number of producers to produce the data.
    int numConsumers; // Number of consumers to consume/process the data.
    int numMilliseconds; // Number milliseconds for the consumer to sleep for after processing/consuming the data.
    int imageToFetch; // Which image segment to fetch
} args; 

const char* const urls[3][3] = 
{
    // Server 1
    {"http://ece252-1.uwaterloo.ca:2530/image?img=1&part= ", 
    "http://ece252-1.uwaterloo.ca:2530/image?img=1&part= ", 
    "http://ece252-1.uwaterloo.ca:2530/image?img=1&part= "},
    // Server 2
    {"http://ece252-2.uwaterloo.ca:2530/image?img=1&part= ", 
    "http://ece252-2.uwaterloo.ca:2530/image?img=2&part= ", 
    "http://ece252-2.uwaterloo.ca:2530/image?img=3&part= "},
    // Server 3
    {"http://ece252-3.uwaterloo.ca:2530/image?img=1&part= ", 
    "http://ece252-3.uwaterloo.ca:2530/image?img=2&part= ", 
    "http://ece252-3.uwaterloo.ca:2530/image?img=3&part= "}
};

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