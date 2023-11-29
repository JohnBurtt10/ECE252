#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <search.h>

typedef struct node {
    char* url;
    struct node* p_next;
} NODE;

typedef struct list {
    NODE* head;
} LIST;

LIST linked_list;

void push_back(char* url){
    NODE* newNode = malloc(sizeof(NODE));
    newNode->url = url;

    NODE* temp = linked_list.head;
    linked_list.head = newNode;
    linked_list.head->p_next = temp;
}

void clean_list(){
    NODE* currNode = linked_list.head;
    NODE* temp;
    while(currNode != NULL){
        temp = currNode;
        currNode = currNode->p_next;
        if (temp->url != NULL){
            free(temp->url);
        }
        free(temp);
    }
}

int main(int argc, char *argv[]){
    ENTRY e, *ep;
    hcreate(500);

    char *url = malloc(256);
    FILE* fp = fopen(argv[1], "r");

    while (fgets(url, 256, fp)){
        e.key = url;
        e.data = (void*) 0;
        ep = hsearch(e, FIND);
        if (ep != NULL){
            puts("Found Duplicate!");
            printf("Duplicate: %s", ep->key);
            free(url);
            return 0;
        } else {
            hsearch(e, ENTER);
            push_back(url);
            url = malloc(256);
        }
    }

    free(url);
    clean_list();
    hdestroy();
    fclose(fp);

    puts("No Duplicates Found");
    return 0;
}