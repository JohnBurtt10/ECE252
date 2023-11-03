/*
 * The code is derived from 
 * Copyright(c) 2018-2019 Yiqing Huang, <yqhuang@uwaterloo.ca>.
 *
 * This software may be freely redistributed under the terms of the X11 License.
 */

/**
 * @brief  stack to push/pop integers.   
 */

#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include "shm_stack.h"
#include <string.h>

/**
 * @brief calculate the total memory that the struct int_stack needs and
 *        the items[size] needs.
 * @param int size maximum number of integers the stack can hold
 * @return return the sum of ISTACK size and the size of the data that
 *         items points to.
 */

int sizeof_shm_stack(int size)
{
    return (sizeof(IMGSTACK) + (sizeof(RECV_BUF) + 10000) * size);
}

/**
 * @brief initialize the ISTACK member fields.
 * @param ISTACK *p points to the starting addr. of an ISTACK struct
 * @param int stack_size max. number of items the stack can hold
 * @return 0 on success; non-zero on failure
 * NOTE:
 * The caller first calls sizeof_shm_stack() to allocate enough memory;
 * then calls the init_shm_stack to initialize the struct
 */
int init_shm_stack(IMGSTACK *p, int stack_size)
{
    if ( p == NULL || stack_size == 0 ) {
        return 1;
    }

    p->size = stack_size;
    p->pos  = -10000;
    p->imageSeqPos = -1;
    p->imageSeq = (int*) ((char *)p + sizeof(IMGSTACK));
    p->imageData = (char *) ((char *)p + sizeof(IMGSTACK) + (sizeof(int*) * stack_size)); // Point immedatiely after imageseq array.

    //init semaphores
    sem_init(&p->imageToFetch_sem, 1, 1);
    sem_init(&p->items_sem, 1, 0);
    sem_init(&p->buffer_sem, 1, 1);
    sem_init(&p->spaces_sem, 1, stack_size);

    return 0;
}

/**
 * @brief create a stack to hold size number of integers and its associated
 *      ISTACK data structure. Put everything in one continous chunk of memory.
 * @param int size maximum number of integers the stack can hold
 * @return NULL if size is 0 or malloc fails
 */

IMGSTACK *create_stack(int size)
{
    int mem_size = 0;
    IMGSTACK *pstack = NULL;
    
    if ( size == 0 ) {
        return NULL;
    }

    mem_size = sizeof_shm_stack(size);
    pstack = malloc(mem_size);

    if ( pstack == NULL ) {
        perror("malloc");
    } else {
        char *p = (char *)pstack;
        pstack->imageData = (char *) (p + sizeof(IMGSTACK));
        pstack->size = size;
        pstack->pos  = -1;
    }

    return pstack;
}

/**
 * @brief release the memory
 * @param ISTACK *p the address of the ISTACK data structure
 */

void destroy_stack(IMGSTACK *p)
{
    if ( p != NULL ) {
        free(p);
    }
}

/**
 * @brief check if the stack is full
 * @param ISTACK *p the address of the ISTACK data structure
 * @return non-zero if the stack is full; zero otherwise
 */

int is_full(IMGSTACK *p)
{
    if ( p == NULL ) {
        return 0;
    }
    return ( p->pos == (p->size*10000 - 10000) );
}

/**
 * @brief check if the stack is empty 
 * @param ISTACK *p the address of the ISTACK data structure
 * @return non-zero if the stack is empty; zero otherwise
 */

int is_empty(IMGSTACK *p)
{
    if ( p == NULL ) {
        return 0;
    }
    return ( p->pos == -10000 );
}

/**
 * @brief push one integer onto the stack 
 * @param ISTACK *p the address of the ISTACK data structure
 * @param char item the integer to be pushed onto the stack 
 * @return 0 on success; non-zero otherwise
 */

int push(IMGSTACK *p, RECV_BUF* image)
{
    if ( p == NULL ) {
        return -1;
    }

    if ( !is_full(p) ) {
        p->pos = p->pos + 10000;
        memcpy(p->imageData + p->pos, image->buf, 10000);
        memcpy(&p->imageSeq[p->imageSeqPos], &image->seq, sizeof(int));
        p->imageSeqPos++;
        return 0;
    } else {
        return -1;
    }
    return 1;
}

/**
 * @brief push one integer onto the stack 
 * @param ISTACK *p the address of the ISTACK data structure
 * @param int *item output parameter to save the integer value 
 *        that pops off the stack 
 * @return 0 on success; non-zero otherwise
 */

int pop(IMGSTACK *p, RECV_BUF* image)
{    
    if (p == NULL) {
        return -1;
    }
    

    if (!is_empty(p)) {
        memcpy(image->buf, p->imageData + p->pos, 10000);
        image->seq = p->imageSeq[p->imageSeqPos];

        p->imageSeqPos--;
        p->pos = p->pos - 10000; // Assuming your stack is 0-based.
        return 0;
    } else {
        return 1;
    }
}

